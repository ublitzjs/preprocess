import {addon} from "./exports.ts";
addon.setSyntax("txt", {
  prefix: "<%",
  end: "%>",
  insertOn: "=",
  removeOn: "!"
}, 30);

console.log(
  "Return of first call",
  addon.cache("a.txt", false, ()=>{
    console.log(
      "Was it cleared?",
      addon.clearCache("a.txt")
    );
  })
)
console.log("async here")
console.log(
  "second call",
  // "true" will be ignored, because it descreasing lifetime of non-temporary buffer is possible ONLY in clearCache.
  addon.cache("a.txt",  true, ()=>{
    console.log("Seems like a.txt is cached AGAIN");
  })
)
