var {createReadStream, createWriteStream} = require("node:fs")
var maxChunkSize = 64*1024;
export function setMaxChunkSize(value){
  maxChunkSize = value;
}
export default async function preprocess(inputName, outputName, removeOn, end){
  var chunkOffset = 0;
  var inputStream = createReadStream(inputName, {encoding: "utf8"}), outputStream = createWriteStream(outputName, {encoding: "utf8"})
  var result = "";
  var offsetToStart, offsetToEnd;
  var chunkOffset = 0;
  var iterator = inputStream[Symbol.asyncIterator]();
  var input = (await iterator.next()).value;
  var waitForDrain; 
  var leftSpace;
  do {
    offsetToStart = input.indexOf(removeOn, chunkOffset)
    if(offsetToStart != -1){
      if(chunkOffset!=offsetToStart) {
        result += input.substring(chunkOffset, offsetToStart)
        if(result.length > maxChunkSize) {
          if(waitForDrain) {await waitForDrain; waitForDrain = undefined};
          if(!outputStream.write(result))
            waitForDrain = new Promise(resolve=>outputStream.once('drain', resolve))
          result = ""
        }
      }
      chunkOffset = offsetToStart + removeOn.length;
      offsetToEnd = input.indexOf(end, chunkOffset);
      if(offsetToEnd != -1){
        chunkOffset = offsetToEnd + end.length;
      } else {
        chunkOffset = 0
        input = (await iterator.next()).value || "";
        offsetToEnd = input.indexOf(end);
        if(offsetToEnd == -1) throw new Error("Removal doesn't end");
        chunkOffset = offsetToEnd + end.length;
      }
    } else {
      if(inputStream.readableEnded){
        result += input.substring(chunkOffset)
        if(waitForDrain) {await waitForDrain; waitForDrain = undefined};
        return new Promise((resolve)=>outputStream.end(result, resolve))
      } else {
        leftSpace = input.length - chunkOffset
        if(leftSpace){
          result += input.substring(chunkOffset, input.length - leftSpace)
          if(leftSpace > removeOn.length - 1){
            if(waitForDrain) {await waitForDrain; waitForDrain = undefined};
            if(!outputStream.write(result))
              waitForDrain = new Promise(resolve=>outputStream.once('drain', resolve))
            result = ""
          }
          chunkOffset = 0;
          input = input.slice(-1 * leftSpace);
        }
        input += (await iterator.next()).value || "";
      }
    }
  } while(true);
}
module.exports = {
  setMaxChunkSize,
  preprocess
}
