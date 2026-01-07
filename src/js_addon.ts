import { createRequire } from "node:module"
import type {
  Cache,
  Purpose,
  SyntaxPositions,
  Status,
  SyntaxBaseType,
  SyntaxPairType
} from "./js_addon_types.d.ts";
var require = createRequire(import.meta.url);
var maxChunkSize = 64*1024;

const addon : {
  processCache(
    template: string,
    mustUseRAM: boolean,
  ): Cache,
  init(
    data: ( SyntaxBaseType & { pattern: RegExp })[],
    maxChunkSize: number,
    executeCachingQueue: (cache: Cache)=>void
  ): SyntaxPairType[];
} = require("../build/preprocess.node");

var syntax: SyntaxPairType[];
const caches: Record<string, Cache> = {}; //external ab
function find(sourcefile: string): Uint8Array {
  var el: SyntaxPairType;
  for(var i = 0; i<syntax.length; i++){
    el = syntax[i];
    if(el.pattern.test(sourcefile)) return el.data;
  }
  throw new Error("NO SYNTAX")
}

var cachingQueue: Map<Cache, ((ok: boolean)=>void)[]> = new Map();
function addCBtoCachingQueue(cache: Cache, cb: (ok: boolean)=>void): any {
  var queue = cachingQueue.get(cache);
  if(!queue) return cachingQueue.set(cache, [cb]);
  queue.push(cb)
}
function executeCachingQueue(cache: Cache): void {
  var ok: boolean = cache.status == 1;
  for(var el of cachingQueue.get(cache)!){ el(ok); }
  cachingQueue.delete(cache)
}

namespace types {
  export type InstructionsT<T> = {
    t_id: number;
    slots?: Record<string, InstructionsT<T> | (InstructionsT<T> | string)[]>;
    data?: Record<string, T> | Record<string, T>[]; // if array - render several times
  };
}
/**@returns {boolean} Whether call was successful. If not - quit immediately*/
async function handleASTForJS<T>(
  instructions: InstructionsT<T>,
  params: {
    templates: string[];
    readCB: (chunk: Uint8Array, done: ()=>void)=>void;
    insertCB: (lookedUpData: T, done: ()=>void)=>void;
    finalizer: (errStatus?: number, errTemplate?: string)=>void
  },
  shared: {data: Record<string, T>[], caches: string[]; aborted: boolean}
): Promise<boolean> {
  var templateName = params.templates[instructions.t_id];
  var currentSyntax = find(templateName)
  var cache: Cache | undefined = caches[templateName];
  if(!cache) {
    const ok = await new Promise<boolean>(resolve=>{
      cache = addon.processCache(templateName, false);
      addCBtoCachingQueue(cache, resolve)
    })
    if(!ok){
      params.finalizer((cache as Cache).status, templateName);
      return false;
    } else if(shared.aborted){
      params.finalizer();
      return false
    }
    caches[templateName] = cache;
  } else if(cache.status == Status.PendingDiskRead){
    const ok = await new Promise(resolve=>addCBtoCachingQueue(cache!, resolve));
    if(!ok){
      params.finalizer((cache as Cache).status, templateName);
      return false;
    } else if(shared.aborted){
      params.finalizer();
      return false
    }
  }
  
  shared.caches[instructions.t_id] = templateName;
  var purpose: Purpose, length: number, i: number = 0, globalOffset: number = 0;
  do {
    purpose = cache.ast![i];
    length = cache.ast![i+1];
    if(purpose == Purpose.JustRead){
      await new Promise<void>(resolve=>params.readCB(new Uint8Array(cache!.data, globalOffset, length), resolve));
      if(shared.aborted) {
        params.finalizer();
        return false
      } 
    } else if (purpose == Purpose.Remove) {
      globalOffset+=
        currentSyntax[SyntaxPositions.prefix]
      + currentSyntax[SyntaxPositions.removeOn]
      + currentSyntax[SyntaxPositions.end];
    } else {
      globalOffset+=
        currentSyntax[SyntaxPositions.prefix]
      + currentSyntax[SyntaxPositions.insertOn]
      var key = new Uint8Array(cache.data!, globalOffset, length).toString()
      var slot = (instructions.slots || {})[key];
      if(slot){
        if(slot instanceof Array){
          for(var el of slot){
            if(typeof el == "string"){
              await new Promise<void>(resolve=>params.insertCB(el as any, resolve));
              if(shared.aborted){
                params.finalizer();
                return false;
              }
            } else {
              if(!await handleASTForJS(el, params, shared)) return false;
            }
          }
        } else {
          await new Promise<void>(resolve=>params.insertCB(slot as any, resolve))
          if(shared.aborted){
            params.finalizer();
            return false;
          }
        }
      } else {
        let data: T | undefined;
        for(let dataStackI = shared.data.length - 1; dataStackI > 0; dataStackI--){
          data = shared.data[dataStackI][key];
          if(data) {
            await new Promise<void>(resolve=>params.insertCB(data!, resolve));
            if(shared.aborted){
              params.finalizer();
              return false;
            }
            break;
          }
        }
        if(!data) throw new Error("NO DATA FOR KEY: " + key);
      }
      globalOffset+=currentSyntax[SyntaxPositions.end];
    }
  } while( (i+=2) < cache.ast!.length );
  shared.data.pop();
  return true;
}
async function streamToJS<T>(
  instructions: InstructionsT<T>,
  params: {
    templates: string[];
    readCB: (chunk: Uint8Array, done: ()=>void)=>void;
    insertCB: (lookedUpData: T, done: ()=>void)=>void;
    finalizer: (errStatus?: number, errTemplate?: string)=>void;
  },
  regAbort: (state: {aborted: boolean})=>void
){
  var shared = {data: [], aborted: false, caches: []}
  regAbort(shared);
  var ok = await handleASTForJS(instructions, params, shared);
  if(ok) params.finalizer();
  for(var templateName of shared.caches){
    var cache = caches[templateName]!;
    if(cache.status < 0 || --cache.busy == 0){
      delete caches[templateName]
    }
  }
}
async function handleASTForFS<T>(
  instructions: InstructionsT<T>,
  params: {
    templates: string[];
    finalizer: (errStatus?: number, errTemplate?: string)=>void
  },
  shared: {data: Record<string, T>[], caches: string[]; aborted: boolean}
): Promise<boolean> {
  var templateName = params.templates[instructions.t_id];
  var currentSyntax = find(templateName)
  var cache: Cache | undefined = caches[templateName];
  if(!cache) {
    const ok = await new Promise<boolean>(resolve=>{
      cache = addon.processCache(templateName, false);
      addCBtoCachingQueue(cache, resolve)
    })
    if(!ok){
      params.finalizer((cache as Cache).status, templateName);
      return false;
    } else if(shared.aborted){
      params.finalizer();
      return false
    }
    caches[templateName] = cache;
  } else if(cache.status == Status.PendingDiskRead){
    const ok = await new Promise(resolve=>addCBtoCachingQueue(cache!, resolve));
    if(!ok){
      params.finalizer((cache as Cache).status, templateName);
      return false;
    } else if(shared.aborted){
      params.finalizer();
      return false
    }
  }
  
  shared.data.push(instructions.data || {});
  shared.caches[instructions.t_id] = templateName;
  var purpose: Purpose, length: number, i: number = 0, globalOffset: number = 0;
  do {
    purpose = cache.ast![i];
    length = cache.ast![i+1];
    if(purpose == Purpose.JustRead){
      await new Promise<void>(resolve=>params.readCB(new Uint8Array(cache!.data, globalOffset, length), resolve));
      if(shared.aborted) {
        params.finalizer();
        return false
      } 
    } else if (purpose == Purpose.Remove) {
      globalOffset+=
        currentSyntax[SyntaxPositions.prefix]
      + currentSyntax[SyntaxPositions.removeOn]
      + currentSyntax[SyntaxPositions.end];
    } else {
      globalOffset+=
        currentSyntax[SyntaxPositions.prefix]
      + currentSyntax[SyntaxPositions.insertOn]
      var key = new Uint8Array(cache.data!, globalOffset, length).toString()
      var slot = (instructions.slots || {})[key];
      if(slot){
        if(slot instanceof Array){
          for(var el of slot){
            if(typeof el == "string"){
              await new Promise<void>(resolve=>params.insertCB(el as any, resolve));
              if(shared.aborted){
                params.finalizer();
                return false;
              }
            } else {
              if(!await handleASTForJS(el, params, shared)) return false;
            }
          }
        } else {
          await new Promise<void>(resolve=>params.insertCB(slot as any, resolve))
          if(shared.aborted){
            params.finalizer();
            return false;
          }
        }
      } else {
        let data: T | undefined;
        for(let dataStackI = shared.data.length - 1; dataStackI > 0; dataStackI--){
          data = shared.data[dataStackI][key];
          if(data) {
            await new Promise<void>(resolve=>params.insertCB(data!, resolve));
            if(shared.aborted){
              params.finalizer();
              return false;
            }
            break;
          }
        }
        if(!data) throw new Error("NO DATA FOR KEY: " + key);
      }
      globalOffset+=currentSyntax[SyntaxPositions.end];
    }
  } while( (i+=2) < cache.ast!.length );
  shared.data.pop();
  return true;
}
async function streamToFS<T>(
  instructions: InstructionsT<T>,
  params: {
    templates: string[];
    readCB: (chunk: Uint8Array, done: ()=>void)=>void;
    insertCB: (lookedUpData: T, done: ()=>void)=>void;
    finalizer: (errStatus?: number, errTemplate?: string)=>void;
  },
  regAbort: (state: {aborted: boolean})=>void
){
  var shared = {data: [], aborted: false, caches: []}
  regAbort(shared);
  var ok = await handleASTForJS(instructions, params, shared);
  if(ok) params.finalizer();
  for(var templateName of shared.caches){
    var cache = caches[templateName]!;
    if(cache.status < 0 || --cache.busy == 0){
      delete caches[templateName]
    }
  }
}
