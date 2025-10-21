import {addon} from "./exports.ts";
import {EventEmitter} from "node:events";
var emitter = new EventEmitter();
addon.createThreadPools(0,1);
addon.setCachingEmitter((name, err)=>emitter.emit(name, err));
addon.setSyntax("txt", {
  prefix: "<%",
  end: "%>",
  insertOn: "=",
  removeOn: "!"
}, 30);
addon.setSyntax("js", {
  prefix: "/*%",
  end: "%*/",
  insertOn: "=",
  removeOn: "REMOVE="
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
console.log(
  "JS: third call to cache ANOTHER file", 
  JSCache("c.txt", (err)=>{
    console.log("JS: third call failed?", err);
  })
);
try{
  JSCache("b.text", ()=>{});
} catch (err){
  console.error("Error from trying to cache file (here its existence doesn't matter) without right syntax", err);
}
JSCache("b.txt", (err)=>{
  console.error("Error for not finding file", err);
});
setTimeout(()=>{
  console.log(
    "JS: Return of fourth call",
    JSCache("a.txt", ()=>{
      console.log(
        "JS: fourth call clear cache ?",
        addon.clearCache("a.txt")
      );
    })
  )
  console.log(
    "JS: fifth call to cache ANOTHER file", 
    JSCache("c.js", ()=>{
      console.log(
        "JS: fifth call cleared ANOTHER file?",
        addon.clearCache("c.js")
      );
    })
  );
}, 400);
setTimeout(()=>{
  addon.Stop();
  console.log(
    "JS: finalizer cleared cache?",
    addon.clearCache("a.txt"),
    addon.clearCache("c.js")
  );
}, 1000);
function JSCache(name: string, cb: (err?: Error)=>void): void{
  if(!addon.cache(name)) emitter.once(name, cb); else cb();
};
