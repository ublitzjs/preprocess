import {addon, JSCache, Status} from "./exports.ts";
addon.createThreadPools(0);
addon.setSyntax([
  {
    prefix: "<%",
    end: "%>",
    insertOn: "=",
    removeOn: "!",
    maxInsertKeyLength: 30,
    pattern: "txt"
  }, {
    pattern: "js",
    maxInsertKeyLength: 30,
    prefix: "/*%",
    end: "%*/",
    insertOn: "=",
    removeOn: "REMOVE="
  }
]);
//
console.log("JS: 1) cache a.txt")
JSCache("a.txt", (status)=>{
  console.log("cached 1) a.txt OK?", status == Status.Ready);
})
console.log(
  "JS: 2) cache a.txt",
)
JSCache("a.txt", ()=>{
  console.log(
    "JS: a.txt 2) can it clear a.txt now?",
    addon.clearCache("a.txt")
  );
})
console.log(
  "JS: 1) cache c.js", 
);
JSCache("c.js", (status)=>{
  console.log("JS: got error from c.js 1)?", status != Status.Ready);
})
try{
  console.log("JS: 1) cache b.text, which has no right syntax");
  JSCache("b.text", ()=>{});
} catch (err){
  console.error("JS: Immmediate error from b.text", (err as Error).message);
}
console.log("JS: 1) cache non-existing b.txt");
JSCache("b.txt", (status)=>{
  console.error("JS: error from worker about b.txt?", Status[status]);
});
setTimeout(()=>{
  console.log(
    "JS: 4) cache a.txt after timeout.",
  )
  JSCache("a.txt", ()=>{
    console.log(
      "JS: 4) try to clear a.txt cache. Success?",
      addon.clearCache("a.txt")
    );
  })

  console.log(
    "JS: 2) cache c.js again", 
    
  );
  JSCache("c.js", ()=>{
      console.log(
        "JS: 2) Cleared c.js?",
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


addon.streamToFS(
  {id:0, keys: {a:"txt", b: {id: 0,keys: {}}}},
  [
    //["a.txt",90],
    "b.txt"
  ],
  "output.txt",
  ()=>{


  }
);
