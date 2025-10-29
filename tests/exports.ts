import {createRequire} from "node:module";
import {EventEmitter} from "node:events"
var require = createRequire(import.meta.url);
 
type FSStreamParams = {
  id: number,
  keys: Record<string, string | FSStreamParams[] | FSStreamParams>
}
type JSStreamParams = {
  id: number;
  keys?: Record<string, JSStreamParams[] | JSStreamParams>;
}
type JSStreamCb = (
  (data: (ArrayBuffer|[number, string][]), release: ()=>void, error: undefined)=>void
)|(
  (data: undefined, release: undefined, error: Error|undefined)=>void
)
var emitter = new EventEmitter();
export enum Status {
    Ready = 1,
    NoFile = -1,
    CantGetSize = -2,
    CantRead = -3,
    CantAllocate = -4
}
export function JSCache(name: string, cb: (status: Status)=>void): void{
  if(!addon.cache(name)) emitter.on(name, cb); else cb(1);
};

export var addon: {
  streamToFS(params: FSStreamParams, templates: string[], output:string, callback: ()=>void): void;
  setCachingEmitter(emit: (key:string, ...args: any[])=>void): void
  // if you NEVER call "cache" - set second param to 0.
  createThreadPools(streamingWorkersAmount: number): void;
  Stop(): void;
  streamToJS(params: JSStreamParams, templaets: string[], callback: JSStreamCb): void;
  setSyntax(params: {
    pattern: string,
    prefix: string;
    insertOn: string;
    removeOn: string;
    end: string;
    maxInsertKeyLength: number
  }[]): void;
  maxChunk(size: number): void;
  /**
   * It is meant to be used in 1 case: if you cached it as a non-temporary before (!!!)
  * @returns true if it was actually freed (or if it was not found at all), or false if it was busy at the moment and was set to "temporary"
  * */
  clearCache(name: string): boolean;
  /**
   * @returns true if template was JUST cached by THIS function call, and FALSE if it was cached by another thread JUST NOW or a while ago. Returning "false" doesn't mean that function failed.
   * @throws error if something failed.
  * */
  cache(name: string): boolean; 
} = require("../build/Release/addon.node")
addon.setCachingEmitter((name, status)=>{console.log("name: ", name);emitter.emit(name, status)});
