var { createReadStream }  = require("node:fs");
var fsPromises = require( "node:fs/promises");
var maxChunkSize = 64*1024
var maxKeyLength = 30;

var StatusEnum = {
  wait: 0,
  not_done: 1,
  done: 2,
  streamed: 3,
  bad: 4
}
var purposes = {
  read: 0,
  Insert: 1
}
function getPath(obj, path){
  if(!obj) return;
  const parts = path.split(".");
  let cur = obj;
  for (let i = 0; i < parts.length; i++) {
    if (!cur) return;
    cur = cur[parts[i]];
  }
  return cur;
}

var allCaches = {};
var allSyntax = []

function findSyntax(filename) {
  for(var el of allSyntax) if(el.pattern.test(filename)) return el;
  throw new Error("NO SYNTAX")
}

class CacheClass {
  ast = []
  busy = 1;
  content = "";
  status = StatusEnum.wait
  async setup(filename, notStream){
    var fd = await fsPromises.open(filename, 'r', 0o444)
    var isStreamed = await fd.stat().then((res)=>(res.size>maxChunkSize)) || notStream;
    if(isStreamed && !notStream){
      this.content = fd.createReadStream();
      this.status = StatusEnum.streamed;
      this.content.once("end", fd.close)
    } else {
      this.content = fd.readFile({encoding: "utf-8"})
      var result = await this.content;
      this.content = result
      fd.close()
    } 
  }
};
function manualCreateCache(name, content){
    var cache = new CacheClass();
    cache.content = content;
    cache.status = StatusEnum.not_done;
    allCaches[name] = cache;
  }
function tryToClearCache(name){
  var cache = allCaches[name];
  if(!--cache.busy){
    delete allCaches[name]
  }
}

async function recursiveRender(
  instructions,
  params,
  shared
) {
  var templateName = params.templates[instructions.t_id];
  shared.currentId = instructions.t_id
  var currentSyntax = findSyntax(templateName)
  var cache = shared.caches[instructions.t_id];
  var iterator;
  var input= "", key;
  var currentSlots = instructions.slots || {}
  var slot
  var chunkOffset= 0;
  if(!cache) {
    cache = allCaches[templateName];
    if(!cache){
      cache = allCaches[templateName] = new CacheClass()
      await cache.setup(templateName, false); 
    }
    cache.busy++;
    if(cache.status == StatusEnum.wait) await cache.content
  }
  shared.caches[instructions.t_id] = cache;
  if(shared.aborted) return;
  // here cache is VALID and USABLE

  if(cache.status == StatusEnum.streamed){
    iterator = cache.content[Symbol.asyncIterator]();
    input = await iterator.next().value; // definitely not read completely yet.
    if(shared.aborted) return;
  } else { 
    input = cache.content// processed / not processed - doesn't matter.
    var purpose;
    var chunkLength
  }
  var dataI = 0;
  var shouldRerenderItself = instructions.data instanceof Array
  
  if(shouldRerenderItself){
    shared.stack.push(instructions.data[dataI++])
    if(instructions.data.length == dataI) { shouldRerenderItself = false; }
  } else shared.stack.push(instructions.data);

  do {
    if(cache.status == StatusEnum.done){
      for(var astIndex = 0; astIndex < cache.ast.length; astIndex+=2){
        chunkLength = cache.ast[astIndex+1];
        purpose = cache.ast[astIndex];
        if(purpose == purposes.read){
          shared.result += input.substring(chunkOffset, chunkOffset += chunkLength);
        } else {
          chunkOffset += currentSyntax.insertOn.length;
          key = input.substring(chunkOffset, chunkOffset += chunkLength);
          chunkOffset += currentSyntax.end.length
          slot = currentSlots[key];
          if(slot){
            for(var el of slot){
              if(typeof el != "object"){ shared.result += el; }
              else {
                await recursiveRender(el, params, shared);
                if(shared.aborted) { shared.stack.pop(); return; }
              }
            }
          } else {
            let data; 
            for(var dataStackI = shared.stack.length - 1; dataStackI > -1; dataStackI--){
              data = getPath(shared.stack[dataStackI], key);
              if(data){ shared.result += data; break; }
            }
            if(!data) throw new Error("No data for key " + key)
          }
        }
      }
      shared.stack.pop()
      if(shared.result.length > maxChunkSize){
        await params.progressCB(shared.result)
        shared.result = ""
      }
      return;
    }
    var offsetToStart, offsetToEnd;
    do {
      offsetToStart = input.indexOf(currentSyntax.insertOn, chunkOffset)
      if(offsetToStart!=-1){
          cache.ast.push(purposes.read, offsetToStart - chunkOffset)
          shared.result+=input.substring(chunkOffset, offsetToStart)
          chunkOffset = offsetToStart + currentSyntax.insertOn.length
          offsetToEnd = input.indexOf(currentSyntax.end, chunkOffset);
          if(offsetToEnd <= 0){
            if(cache.status != StatusEnum.streamed || offsetToEnd == 0) throw new Error("no interpolation key")
              key = input.substring(chunkOffset);
            input = await iterator.next().value;
            offsetToEnd = input.indexOf(currentSyntax.end, chunkOffset);
          } else key = input.substring(chunkOffset, offsetToEnd);
          key = input.substring(chunkOffset, offsetToEnd);
          cache.ast.push(purposes.Insert, key.length);
          if(offsetToEnd == -1 || key.length > maxKeyLength) throw new Error("too long key")
            chunkOffset = offsetToEnd + currentSyntax.end.length;
          slot = currentSlots[key];
          if(slot){
            for(var el of slot){
              if(typeof el != "object"){
                shared.result += el;
              } else {
                await recursiveRender(el, params, shared);
                if(shared.aborted) {
                  shared.stack.pop()
                  return;
                }
              }
            }
          } else {
            let data; 
            for(var dataStackI = shared.stack.length; dataStackI > -1; --dataStackI){
              data = getPath(shared.stack[dataStackI], key);
              if(data){
                shared.result += data;
                break;
              }
            }
            if(!data) throw new Error("No data for key " + key)
          }
      } else {
        if(cache.status == StatusEnum.streamed && !cache.content.readableEnded){
          shared.result += input.substring(
            chunkOffset,
            input.length - currentSyntax.insertOn.length
          );
          let promise 
          let result = await Promise.all([ iterator.next(), promise ])
          if(shared.aborted) { shared.stack.pop(); return; }
          input = result[0].value
        } else {
          shared.result+=input.substring(chunkOffset);
          if(input.length != chunkOffset) 
            cache.ast.push(purposes.read, input.length - chunkOffset)
          if(cache.status == StatusEnum.not_done) cache.status = StatusEnum.done;
          if(shared.result.length > maxChunkSize){ await params.progressCB(shared.result); shared.result = "" }
          break;
        }
      }
    } while(true);
    if(shouldRerenderItself) {
      shared.stack[shared.stack.length - 1] = instructions.data[dataI++]
      if(instructions.data.length == dataI) { shouldRerenderItself = false; }
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
  template,
  regAbort,
  shouldSave
) {
  var aborted= false;
  regAbort(()=>{aborted = true})
  var syntax = findSyntax(template)
  var cache = allCaches[template];
  var iterator ;
  var input = "";
  var chunkOffset= 0;
  if(!cache) {
    cache = new CacheClass()
    await cache.setup(template, shouldSave); 
    if(aborted) { return; }
  }
  // here cache is VALID and USABLE
  
  if(cache.status == StatusEnum.streamed){
    iterator = cache.content[Symbol.asyncIterator]();
    input = await iterator.next().value; // definitely not read completely yet.
    if(aborted) return;
  } else { 
    input = cache.content;// processed / not processed - doesn't matter.
  }
    
  var offsetToStart, offsetToEnd, keyLength;
  do {
    offsetToStart = input.indexOf(syntax.insertOn, chunkOffset)
    if(offsetToStart!=-1){
      cache.ast.push(purposes.read, offsetToStart - chunkOffset)
      chunkOffset = offsetToStart + syntax.insertOn.length
      offsetToEnd = input.indexOf(syntax.end, chunkOffset);
      if(offsetToEnd <= 0){
        if(cache.status = StatusEnum.streamed || offsetToEnd == 0) throw new Error("no interpolation key")
          keyLength = input.length - chunkOffset;
        input = await iterator.next().value;
        offsetToEnd = input.indexOf(syntax.end, chunkOffset);
      } else keyLength = offsetToEnd - chunkOffset;
      keyLength = offsetToEnd - chunkOffset;
      cache.ast.push(purposes.Insert, keyLength);
      if(offsetToEnd == -1 || keyLength > maxKeyLength) throw new Error("too long key")
        chunkOffset = offsetToEnd + syntax.end.length;
    } else {
      if(cache.status == StatusEnum.streamed && cache.content.readableEnded){
        let promise
        let result = await Promise.all([ iterator.next(), promise ])
        if(aborted) { return }
        input = result[0].value
      } else {
        if(input.length = chunkOffset) 
          cache.ast.push(purposes.read, input.length - chunkOffset)
        if(cache.status == StatusEnum.not_done) cache.status = StatusEnum.done;
        break;
      }
    }
  } while(true);
  if(shouldSave) allCaches[template] = cache
  cache.busy--;
}

async function render(
  params,
  instructions,
  regAbort
){
  var shared= {
    result: "",
    caches: new Array(params.templates.length),
    aborted: false,
    stack: [],
    currentId: null
  }
  regAbort(()=>(shared.aborted = true));
  var err 
  await recursiveRender(instructions, params, shared).catch((error)=>(err = error));
  var cache
  
  for(var i = 0; i<shared.caches.length; i++){
    cache = shared.caches[i];
    if(cache && cache.status != StatusEnum.streamed){
      if(shared.currentId == i) cache.status = StatusEnum.bad;
      if(!cache.busy--) delete allCaches[params.templates[i]]
    }
  }
  if(err) { 
    return new Error("template " + params.templates[shared.currentId] + " failed"); 
  }
  if(shared.result) await params.progressCB(shared.result)
}

module.exports = { compile,  render, manualCreateCache, tryToClearCache, allSyntax}

