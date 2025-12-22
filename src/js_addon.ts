import { createRequire } from "node:module"
import type {RecognizedString} from "uWebSockets.js"
var require = createRequire(import.meta.url);
var maxChunkSize = 64*1024;
enum Status { // < 0 - error, > 0 - good
  AST_Ready = 2,
  JustInMemory = 1,
  PendingDiskRead = 0,
  CantUseFile = -1,
  CantAllocate = -2,
  AST_Failed = -3
};
enum Action {
  JustRead = 0,
  Insert = 1,
  Remove = 2,
};
const addon : {
  processCurrentChunk(
    syntax: ArrayBuffer,
    state: ArrayBuffer,
    cache: ArrayBuffer,
    cb: (
      err: number|null,
      ast: Uint32Array,
      data: ArrayBuffer
    )=>void
  ) : void;
  createCache(
    template: string,
    mustBeGlobal: boolean,
    cb: (
      err: number | null,
      data: null | ArrayBuffer,
      fileSize: bigint
    )=>void
  ): boolean, // isGlobal
  tryDeleteGlobalCache(template: string): boolean;
  cleanUpTaskCaches(
    data: ArrayBuffer[],
    globalCaches: Record<string,
    ArrayBuffer>
  ): void;
  createStackLevel(): ArrayBuffer;
} = require("../build/Release/addon.node");
const syntax: Map<RegExp, ArrayBuffer> = new Map<RegExp, ArrayBuffer>();
const caches: Record<string, ArrayBuffer> = {}; //external ab
syntax.set(new RegExp("\.js"), new ArrayBuffer());
function find(sourcefile: string): ArrayBuffer {
  var value: ArrayBuffer;
  syntax.forEach(
    (val, key)=>(key.test(sourcefile) ? value = val : undefined)
  );
  if(value!)
    return value;
  else 
    throw new Error("no syntax");
}

type InstructionsT = {
  t_id: number;
  data: Record<string, any>;
  slots: Record<string, InstructionsT | (InstructionsT | string)[]>;
};
async function streamToJS(
  templates: string[],
  instructions: InstructionsT,
  progressCB: (chunk: RecognizedString, done: ()=>void)=>void,
  finalizer: (errStatus: number, errTemplate: string)=>void
){
  var bookedCaches: ArrayBuffer[] = new Array(templates.length);
  var stack = [{
    ast_chunk_offset: 0,
    ast_index: 0,
    ast_last_size: 0,
    level_index: 0,
    level_state: addon.createStackLevel()
  }];
  var currentTemplateName = templates[instructions.t_id];
  var currentSyntax = find(currentTemplateName)
  var currentCache: ArrayBuffer | undefined = caches[currentTemplateName];
  if(!currentCache && !(await new Promise<boolean>((resolve)=>{
    addon.createCache(templates[0], false, (err, data, fileSize)=>{
      if(err) {
        addon.cleanUpTaskCaches(bookedCaches,  caches);
        finalizer(0, currentTemplateName);
        resolve(false);
      } else {
        currentCache = data!;
        if(fileSize <= BigInt(maxChunkSize)) caches[currentTemplateName] = currentCache;
        resolve(true);
      }
    })
  })) ){
    return;
  }

  addon.processCurrentChunk(currentSyntax, stack[0].level_state, currentCache, (errStatus, ast, data)=>{
    if(errStatus){
      return
    }
    for(var i = 0; i<ast.length; i+=2){
      var length = ast[i];
      var purpose = ast[i+1];
      switch (purpose){
        case Action.JustRead:
          progressCB(new Uint8Array(data, 
          break;
      }
    }
  });


}
