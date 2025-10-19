import {addon} from "./exports.ts";
addon.setSyntax("txt", {
  prefix: "<%",
  end: "%>",
  insertOn: "=",
  removeOn: "!"
}, 30);

console.log(
  "JS: Return of first call",
  addon.cache("a.txt", true, ()=>{
    console.log("first cb");
  })
)
console.log(
  "JS: second call",
  addon.cache("a.txt", false, ()=>{
    console.log("second cb");
    console.log(
      "JS: second call cleared cache?",
      addon.clearCache("a.txt")
    );
  })
)
console.log(
  "JS: Return of senond call",
  addon.cache("a.txt", false, ()=>{
    console.log(
      "JS: third call clear cache ?",
      addon.clearCache("a.txt")
    );
  })
)
setTimeout(()=>{console.log("timeout")}, 1000);
