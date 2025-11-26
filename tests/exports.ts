import {createRequire} from "node:module";
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
export enum Status {
    Ready = 1,
    NoFile = -1,
    CantGetSize = -2,
    CantRead = -3,
    CantAllocate = -4
}
/**
 * first string in tuple - name of template.
* here first "number" in tuple means "how many times at least this template will be used throughout the function". Great for optimizing memory usage (as soon as used all times -> cache tries to get deleted). 0 (default) == no limit and keep in memory until function ends
  third boolean in tuple - if template is "AST optimized". It can happen only after using .compile with "string" as a "save" param.
* */
export type templatesList = (string | [string, number, boolean?])[]; 
export var addon: {
  streamToFS(params: FSStreamParams, templates: templatesList, output:string, callback: (error?: [Status, string]) /*string - cache's filename*/ =>void): void;
  // if you NEVER call "cache" - set second param to 0.
  createThreadPools(streamingWorkersAmount: number): void;
  Stop(): void;
  /**
  * This is the best way to ensure your template has right syntax and get a notification if not. 
  * It performs all checks and, if "save == true", saves it to globally-accessible caches as the whole files with in-memory optimization (just save WHERE syntax does WHAT). If save == false, file is streamed with chunks staying within maxChunkSize. if save = string - saves your template with optimized AST format in the beginning. So in production you can take your "src" templates, optimize to some folder and delete other ones - speed boost, less processing headache, and if deleted previous - more disk space.
  * */
  compile(filename: string, save: boolean, output: string | undefined,  cb: (status: Status)=>void): void;
  streamToJS(params: JSStreamParams, templates: templatesList, callback: JSStreamCb): void;
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
   * It doesn't notify you when it finishes. second param means whether template already has ast inside file or not.
   * @throws error if something failed.
  * */
  cache(name: string, ast: boolean): void; 
} = require("../build/Release/addon.node")
