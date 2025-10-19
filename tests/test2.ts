import {addon} from "./exports.ts";
addon.setSyntax("txt", {
  prefix: "<%",
  end: "%>",
  insertOn: "=",
  removeOn: "!"
}, 30);

console.log(
  "JS: Return of first call",
  addon.cache("a.txt", false, ()=>{
    console.log(
      "JS: Was it cleared?",
      addon.clearCache("a.txt")
    );
  })
)
console.log(
  "JS: second call",
// "true" will be ignored, because it descreasing lifetime of non-temporary buffer is possible ONLY in clearCache.
  addon.cache("a.txt",  true, ()=>{
    console.log("JS: Seems like a.txt was already caching");
  })
)
