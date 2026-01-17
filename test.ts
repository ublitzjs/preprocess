import render, {allSyntax} from "./src/addon.ts";
allSyntax.push({
  prefix: "<!--",
  end: "-->",
  insertOn: "=",
  removeOn: "RM-->",
  maxActionL: 5,
  pattern: new RegExp("html")
})

await render({
  templates: ["templates/layout.html"],
  progressCB(data){
    console.log(data)
  }
}, {
  t_id: 0,
  data: [{
    footer: ["FOOTER1"],
    body: ["BODY1"]
  }, {
    footer: ["FOOTER2"],
    body: ["BODY2"]
  }]
}, ()=>{})
