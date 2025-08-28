import {dirname} from "node:path";
import {mkdirSync, existsSync} from "node:fs";
export class ProcessRequirements {
  /**@type{string}*/
  inputFilename;
  /**@type{string}*/
  outputFilename
  /**@type{string}*/
  startPreprocessor
  /**@type{string}*/
  endPreprocessor
  constructor(input, out, start, end){
    this.inputFilename = input;
    this.outputFilename = out;
    this.startPreprocessor = start;
    this.endPreprocessor = end;
    var parent_path = dirname(out);
    if(parent_path != '.' && !existsSync(parent_path)) mkdirSync(parent_path, {recursive:true})
  }
}
