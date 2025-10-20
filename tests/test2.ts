import {addon} from "./exports.ts";
import {EventEmitter} from "node:events";
var emitter = new EventEmitter();
addon.setCachingEmitter((name, err)=>emitter.emit(name, err));
addon.setSyntax("txt", {
  prefix: "<%",
  end: "%>",
  insertOn: "=",
  removeOn: "!"
}, 30);
//
console.log(
  "JS: Return of first call",
  JSCache("a.txt", (err)=>{
    console.log("first cb", err);
  })
)

console.log(
  "JS: Return of second call",
  JSCache("a.txt", ()=>{
    console.log(
      "JS: second call cleared cache?",
      addon.clearCache("a.txt")
    );
  })
)
setTimeout(()=>{
  console.log(
    "JS: Return of third call",
    JSCache("a.txt", ()=>{
      console.log(
        "JS: third call clear cache ?",
        addon.clearCache("a.txt")
      );
    })
  )
}, 400);
setTimeout(()=>{
  addon.Stop();
  console.log(
    "JS: finalizer cleared cache?",
    addon.clearCache("a.txt")
  );
}, 1000);
function JSCache(name: string, cb: (err?: Error)=>void): boolean {
  var wasCachedBefore = addon.cache(name);    
  if(!wasCachedBefore) emitter.once(name, cb);
  else cb();
  return wasCachedBefore;
};
