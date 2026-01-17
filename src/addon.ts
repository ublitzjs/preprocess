import type * as types from "./js_addon_types.ts"
import { type ReadStream, createReadStream } from "node:fs";
import fsPromises from "node:fs/promises";
import path from "node:path"
import {StatusEnum, PurposeEnum} from "./js_addon_types.ts"
var maxChunkSize: number = 16*1024
var maxKeyLength = 50;
var astPath: string;
var templatesWorkingDir: string;
function getPath(obj: Record<string, any> | undefined, path: string) : any {
  if(!obj) return;
  const parts = path.split(".");
  let cur = obj;
  for (let i = 0; i < parts.length; i++) {
    if (!cur) return;
    cur = cur[parts[i]];
  }
  return cur;
}

var allCaches: Record<string, Cache> = {};
var allSyntax: types.SyntaxType[] = []

export function findSyntax(filename: string): types.SyntaxType {
  for(var el of allSyntax) if(el.pattern.test(filename)) return el;
  throw new Error("NO SYNTAX")
}

class Cache implements types.CacheT {
  ast: Uint32Array | Array<number> = []
  busy: number = 1;
  content: string | Promise<string> | ReadStream = "";
  status: types.StatusEnum = StatusEnum.pending
  async setup(filename: string, notStream: boolean, hasAST: boolean){
    var fd = await fsPromises.open(filename, 'r', 0o444)
    var isStreamed = await fd.stat().then((res)=>(res.size>maxChunkSize)) || notStream;
    if(isStreamed){
      this.content = fd.createReadStream();
      this.status = StatusEnum.streamed;
      this.content.once("end", fd.close)
      if(hasAST){
        var result = await fsPromises.readFile(
          path.resolve(
            astPath,
            path.resolve(templatesWorkingDir, filename)
          ),
        )
        this.ast = new Uint32Array(result.buffer)
      }
    } else {
      this.content = fd.readFile({encoding: "utf-8"})
      await Promise.all([
        this.content,
        hasAST ? fsPromises.readFile(
          path.resolve(
            astPath,
            path.resolve(templatesWorkingDir, filename)
          ),
        ) : undefined
      ]).then(result=>{
        this.content = result[0]
        if(hasAST) this.ast = new Uint32Array(result[1]!.buffer)
      })
      fd.close()
    } 
  }
};
async function recursiveRender(
  instructions: types.Instructions,
  params: types.UserParams,
  shared: types.SharedT
) {
  var templateName = params.templates[instructions.t_id];
  var currentSyntax = findSyntax(templateName)
  var cache: Cache | undefined = allCaches[templateName];
  var iterator: NodeJS.AsyncIterator<string, undefined, any>;
  var input: string = "", key: string;
  var currentSlots = instructions.slots || {}
  var slot: (types.Instructions | string)[] | undefined;
  var chunkOffset: number = 0;
  if(!cache) {
    cache = allCaches[templateName] = new Cache()
    await cache.setup(templateName, false, false); 
  } else {
    cache.busy++;
    if(cache.status == StatusEnum.pending) await (cache.content as Promise<string>)
  }
  shared.caches[instructions.t_id] = cache;
  if(shared.aborted) return;
  // here cache is VALID and USABLE
  

  if(cache.status == StatusEnum.streamed){
    iterator = (cache.content as ReadStream)[Symbol.asyncIterator]();
    input = (await iterator.next()).value!; // definitely not read completely yet.
    if(shared.aborted) return;
  } else { 
    input = cache.content as string;// processed / not processed - doesn't matter.
    var purpose: types.PurposeEnum;
    var chunkLength: number;
  }
  var dataI = 0;
  var shouldRerenderItself = instructions.data instanceof Array
  
  if(shouldRerenderItself){
    shared.stack.push(instructions.data![dataI++])
    if(instructions.data!.length == dataI) { shouldRerenderItself = false; }
  } else shared.stack.push(instructions.data);
  do {
    if(cache.status == StatusEnum.processed){
      for(var astIndex = 0; astIndex < cache.ast.length; astIndex+=2){
        chunkLength = cache.ast![astIndex+1];
        purpose = cache.ast![astIndex];
        if(purpose == PurposeEnum.JustRead){
          shared.result += input.substring(chunkOffset, chunkOffset += chunkLength);
        } else if(purpose == PurposeEnum.Remove){
          chunkOffset +=
            currentSyntax.prefix.length +
            currentSyntax.removeOn.length +
            chunkLength + 
            currentSyntax.end.length
        } else {
          chunkOffset+=
            currentSyntax.prefix.length +
            currentSyntax.insertOn.length;
          key = input.substring(chunkOffset, chunkOffset += chunkLength);
          chunkOffset += currentSyntax.end.length
          slot = currentSlots[key];
          if(slot){
            for(var el of slot){
              if(typeof el == "string"){
                shared.result += el;
                //if(shared.result.length > maxChunkSize) await params.progressCB(shared.result as string);
                //shared.result = ""
              } else {
                await recursiveRender(el as types.Instructions, params, shared);
                if(shared.aborted) {
                  shared.stack.pop()
                  return;
                }
              }
            }
          } else {
            let data: string | undefined; 
            for(var dataStackI = shared.stack.length - 1; dataStackI > -1; dataStackI--){
              data = getPath(shared.stack[dataStackI], key);
              if(data){
                shared.result += data;
                //if(shared.result.length > maxChunkSize) {
                //  await params.progressCB(shared.result as string) 
                //  shared.result = ""
                //}
                break;
              }
            }
            if(!data) throw new Error("No data for key " + key)
          }
        }
      }
      shared.stack.pop()
      if(shared.result){
        await params.progressCB(shared.result)
        shared.result = ""
      }
      return;
    }
    var offsetToStart: number, currentAction: string, offsetToEnd: number;
    do {
      offsetToStart = input!.indexOf(currentSyntax.prefix, chunkOffset)
      if(offsetToStart!=-1){
        currentAction = input.substring(offsetToStart + currentSyntax.prefix.length, offsetToStart + currentSyntax.prefix.length + currentSyntax.maxActionL);
        if(currentAction.startsWith(currentSyntax.insertOn)){
          (cache.ast as Array<number>).push(PurposeEnum.JustRead, offsetToStart - chunkOffset)
          shared.result+=input!.substring(chunkOffset, offsetToStart)
          chunkOffset = offsetToStart + currentSyntax.prefix.length + currentSyntax.insertOn.length;
          offsetToEnd = input!.indexOf(currentSyntax.end, chunkOffset);
          if(offsetToEnd <= 0){
            if(cache.status != StatusEnum.streamed || offsetToEnd == 0) throw new Error("no interpolation key")
              key = input.substring(chunkOffset);
            input = (await iterator!.next()).value!;
            offsetToEnd = input!.indexOf(currentSyntax.end, chunkOffset);
          } else key = input.substring(chunkOffset, offsetToEnd);
          key = input!.substring(chunkOffset, offsetToEnd);
          (cache.ast as Array<number>).push(PurposeEnum.Insert, key.length);
          if(offsetToEnd == -1 || key.length > maxKeyLength) throw new Error("too long key")
            chunkOffset = offsetToEnd + currentSyntax.end.length;
          slot = currentSlots[key];
          if(slot){
            for(var el of slot){
              if(typeof el == "string"){
                shared.result += el;
              } else {
                await recursiveRender(el as types.Instructions, params, shared);
                if(shared.aborted) {
                  shared.stack.pop()
                  return;
                }
              }
            }
          } else {
            let data: string | undefined; 
            for(var dataStackI = shared.stack.length; dataStackI > -1; --dataStackI){
              data = getPath(shared.stack[dataStackI], key);
              if(data){
                shared.result += data;
                break;
              }
            }
            if(!data) throw new Error("No data for key " + key)
          }
        } else if (currentAction.startsWith(currentSyntax.removeOn)){
          shared.result+=input!.substring(chunkOffset, offsetToStart);
          (cache.ast as Array<number>).push(PurposeEnum.JustRead, offsetToStart - chunkOffset)
          chunkOffset = offsetToStart + currentSyntax.prefix.length + currentSyntax.removeOn.length
          offsetToEnd = input!.indexOf(currentSyntax.end, chunkOffset);
          if(offsetToEnd != -1){
            chunkOffset = offsetToEnd + currentSyntax.end.length;
            (cache.ast as Array<number>).push(PurposeEnum.Remove, offsetToEnd - chunkOffset)
          } else {
            if(cache.status != StatusEnum.streamed || (cache.content as ReadStream).readableEnded) {
              (cache.ast as number[]).push(PurposeEnum.Remove, input.length - chunkOffset)
              if(cache.status == StatusEnum.not_processed) cache.status = StatusEnum.processed;
              break;
            }
            else {
              chunkOffset = 0
              let removalLength = input.length - chunkOffset;
              input = (await iterator!.next()).value!;
              offsetToEnd = input!.indexOf(currentSyntax.end);
              if(offsetToEnd == -1) throw new Error("Removal doesn't end");
              removalLength += offsetToEnd;
              chunkOffset = offsetToEnd + currentSyntax.end.length;
            };
          }
        } else {
          let afterPrefix = offsetToStart + currentSyntax.prefix.length;
          shared.result += input.slice(chunkOffset, afterPrefix);
          (cache.ast as number[]).push(PurposeEnum.JustRead, afterPrefix - chunkOffset) 
          chunkOffset = afterPrefix;
        }
      } else {
        if(cache.status == StatusEnum.streamed && !(cache.content as ReadStream).readableEnded){
          shared.result += input.substring(
            chunkOffset,
            input.length - currentSyntax.prefix.length - currentSyntax.maxActionL
          );
          let promise: Promise<any> | undefined;
          let result = await Promise.all([
            iterator!.next(),
            promise
          ])
          if(shared.aborted) {
            shared.stack.pop()
            return;
          }
          input = result[0].value!
        } else {
          shared.result+=input!.substring(chunkOffset);
          if(input.length != chunkOffset) (cache.ast as number[]).push(PurposeEnum.JustRead, input.length - chunkOffset)
            if(cache.status == StatusEnum.not_processed) cache.status = StatusEnum.processed;
          if(shared.result){
            await params.progressCB(shared.result)
            shared.result = ""
          }
          break;
        }
      }
    } while(true);
    if(shouldRerenderItself) {
      shared.stack[shared.stack.length - 1] = instructions.data![dataI++]
      if(instructions.data!.length == dataI) { shouldRerenderItself = false; }
      chunkOffset = 0;
      if(cache.status == StatusEnum.streamed){
        cache.content = createReadStream(params.templates[instructions.t_id])
        iterator = cache.content[Symbol.asyncIterator]()
      }
    } else {
      shared.stack.pop()
      return;
    };
  } while(true);
}
async function compile(
  templateName: string,
  regAbort: (cb:()=>void)=>void,
  save: boolean,
){
  var aborted = false;
  regAbort(()=>aborted = true);
  var currentSyntax = findSyntax(templateName)
  var cache: Cache | undefined = allCaches[templateName];
  var iterator: NodeJS.AsyncIterator<string, undefined, any>;
  var input: string = "", key: string;
  var chunkOffset: number = 0;
  if(!cache) {
    cache = allCaches[templateName] = new Cache()
    await cache.setup(templateName, save, false); 
  } else {
    cache.busy++;
    if(cache.status == StatusEnum.pending) await (cache.content as Promise<string>)
    else if(cache.status == StatusEnum.processed) return;
  }
  if(aborted) return;
  if(cache.status == StatusEnum.streamed){
    iterator = (cache.content as ReadStream)[Symbol.asyncIterator]();
    input = (await iterator.next()).value!; // definitely not read completely yet.
    if(aborted) return ;
  }
  var offsetToStart: number, currentAction: string, offsetToEnd: number;
  do {
    offsetToStart = input!.indexOf(currentSyntax.prefix, chunkOffset)
    if(offsetToStart!=-1){
      currentAction = input.substring(offsetToStart + currentSyntax.prefix.length, offsetToStart + currentSyntax.prefix.length + currentSyntax.maxActionL);
      if(currentAction.startsWith(currentSyntax.insertOn)){
        (cache.ast as Array<number>).push(PurposeEnum.JustRead, offsetToStart - chunkOffset)
        chunkOffset = offsetToStart + currentSyntax.prefix.length + currentSyntax.insertOn.length;
        offsetToEnd = input!.indexOf(currentSyntax.end, chunkOffset);
        if(offsetToEnd <= 0){
          if(cache.status != StatusEnum.streamed || offsetToEnd == 0) throw new Error("no interpolation key")
            key = input.substring(chunkOffset);
          input = (await iterator!.next()).value!;
          offsetToEnd = input!.indexOf(currentSyntax.end, chunkOffset);
        } else key = input.substring(chunkOffset, offsetToEnd);
        key = input!.substring(chunkOffset, offsetToEnd);
        (cache.ast as Array<number>).push(PurposeEnum.Insert, key.length);
        if(offsetToEnd == -1 || key.length > maxKeyLength) throw new Error("too long key")
          chunkOffset = offsetToEnd + currentSyntax.end.length;
      } else if (currentAction.startsWith(currentSyntax.removeOn)){
        (cache.ast as Array<number>).push(PurposeEnum.JustRead, offsetToStart - chunkOffset)
        chunkOffset = offsetToStart + currentSyntax.prefix.length + currentSyntax.removeOn.length
        offsetToEnd = input!.indexOf(currentSyntax.end, chunkOffset);
        if(offsetToEnd != -1){
          chunkOffset = offsetToEnd + currentSyntax.end.length;
          (cache.ast as Array<number>).push(PurposeEnum.Remove, offsetToEnd - chunkOffset)
        } else {
          if(cache.status != StatusEnum.streamed || (cache.content as ReadStream).readableEnded) {
            (cache.ast as number[]).push(PurposeEnum.Remove, input.length - chunkOffset)
            if(cache.status == StatusEnum.not_processed) cache.status = StatusEnum.processed;
            break;
          }
          else {
            chunkOffset = 0
            let removalLength = input.length - chunkOffset;
            input = (await iterator!.next()).value!;
            offsetToEnd = input!.indexOf(currentSyntax.end);
            if(offsetToEnd == -1) throw new Error("Removal doesn't end");
            removalLength += offsetToEnd;
            chunkOffset = offsetToEnd + currentSyntax.end.length;
          };
        }
      } else {
        let afterPrefix = offsetToStart + currentSyntax.prefix.length;
        (cache.ast as number[]).push(PurposeEnum.JustRead, afterPrefix - chunkOffset) 
        chunkOffset = afterPrefix;
      }
    } else {
      if(cache.status == StatusEnum.streamed && !(cache.content as ReadStream).readableEnded){
        let promise: Promise<any> | undefined;
        let result = await Promise.all([
          iterator!.next(),
          promise
        ])
        if(aborted) return;
        input = result[0].value!
      } else {
        if(input.length != chunkOffset) (cache.ast as number[]).push(PurposeEnum.JustRead, input.length - chunkOffset)
        if(cache.status == StatusEnum.not_processed) cache.status = StatusEnum.processed;
        break;
      }
    }
  } while(true);
  if(cache.status == StatusEnum.streamed){
    
  } else if(!save && !--cache.busy){
    delete allCaches[templateName]
  }
}

async function render(
  params: types.UserParams,
  instructions: types.Instructions,
  regAbort: (cb:()=>void)=>void,
){
  var shared: types.SharedT = {
    result: "",
    caches: new Array(params.templates.length),
    aborted: false,
    stack: []
  }
  regAbort(()=>(shared.aborted = true));
  var err: unknown
  await recursiveRender(instructions, params, shared).catch((error)=>(err = error));
  var cache: types.CacheT;
  for(var i = 0; i<shared.caches.length; i++){
    cache = shared.caches[i];
    if(cache && !cache.busy-- && cache.status != StatusEnum.streamed){
      delete allCaches[params.templates[i]]
    }
  }
  if(err) throw err;
  if(shared.result) await params.progressCB(shared.result)
}

export {getPath, render as default, Cache, allCaches, allSyntax, StatusEnum}

