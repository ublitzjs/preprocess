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

type SyntaxBaseType = {
    prefix: string;
    insertOn: string;
    removeOn: string;
    end: string;
}

type SyntaxPairType = {
  pattern: RegExp;
  data: Uint8Array;
}
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
  init(data: (SyntaxBaseType & {
    pattern: RegExp
  })[]): SyntaxPairType[];
} = require("../build/addon.node");

var syntax: SyntaxPairType[];
const caches: Record<string, ArrayBuffer> = {}; //external ab
function find(sourcefile: string): Uint8Array {
  var el: SyntaxPairType;
  for(var i = 0; i<syntax.length; i++){
    el = syntax[i];
    if(el.pattern.test(sourcefile)) return el.data;
  }
  throw new Error("NO SYNTAX")
}
function init(data: (SyntaxBaseType & {pattern: RegExp})[]){
  syntax = addon.init(data)
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
