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
console.log("JS: 1) cache a.txt")
JSCache("a.txt", (err)=>{
  console.log("JS: got err from a.txt 1)?", err);
})
console.log(
  "JS: 2) cache a.txt",
)
JSCache("a.txt", ()=>{
  console.log(
    "JS: Can it clear a.txt now?",
    addon.clearCache("a.txt")
  );
})
console.log(
  "JS: 1) cache c.js", 
);
JSCache("c.js", (err)=>{
  console.log("JS: got error from c.js 1)?", err);
})
try{
  console.log("JS: 1) cache b.text, which has no right syntax");
  JSCache("b.text", ()=>{});
} catch (err){
  console.error("JS: Immmediate error from b.text", (err as Error).message );
}
console.log("JS: 1) cache non-existing b.txt");
JSCache("b.txt", (err)=>{
  console.error("JS: error from worker about b.txt", (err as Error).message);
});
setTimeout(()=>{
  console.log(
    "JS: 4) cache a.txt after timeout.",
      )
      JSCache("a.txt", ()=>{
      console.log(
        "JS: try to clear a.txt cache. Success?",
        addon.clearCache("a.txt")
      );
    })

  console.log(
    "JS: 2) cache c.js again", 
    
  );
  JSCache("c.js", ()=>{
      console.log(
        "JS: Cleared c.js?",
        addon.clearCache("c.js")
      );
    })
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
