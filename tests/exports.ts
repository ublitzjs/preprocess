import {createRequire} from "node:module";
var require = createRequire(import.meta.url);
 
type FSStreamParams = {
  name: string;
  keys: Record<string, string | FSStreamParams[]>
}
type JSStreamParams = {
  name: string;
  id: number;
  other?: Record<string, JSStreamParams[]>;
}
type JSStreamCb = (
  (data: (ArrayBuffer|[number, string][]), release: ()=>void, error: undefined)=>void
)|(
  (data: undefined, release: undefined, error: Error|undefined)=>void
)
export var addon: {
  streamToFS(params: FSStreamParams, output:string, callback: ()=>void): void;
  streamToJS(params: JSStreamParams, callback: JSStreamCb): void;
  setSyntax(pattern: string, params: {
    prefix: string;
    insertOn: string;
    removeOn: string;
    end: string;
  }, maxInsertKeyLength: number): void;
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
  cache(name: string, isTemporary: boolean, cb: ()=>void): boolean; // callback is async (unless you race-condition happened and function is just waiting until other thread caches a template. 
} = require("../build/Release/addon.node")
