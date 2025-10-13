import { RecognizedString } from "uWebSockets.js";
var addon:any;
await new Promise<void>((resolve)=>{
  addon.streamToFS({
    name: "a.template",
    keys: {
      "key": "value",
      "component": [{
        name: "b.template",
        keys: {
          "abcd": "bfgfgfgfgfgf",
          "hello": {
            name: "c.template",
            keys: {

            }
          }
        }
      }, {
        name: "d.template",
      }]
    }
  }, "path/to/output", resolve);
});
await new Promise<void>((resolve)=>{
  addon.streamToFS({
    name: "a.template",
    keys: {
      "templates": [{
        name: "b.txt",
        keys: {
          b:1
        }
      }, {
        name: "b.txt",
        keys: {
          b:2
        }
      }]
    }
  }, "path/to/output", resolve);
});


var insertData: Record<string, RecognizedString>[] = [
  {"key": "value"},
  {"key": "value1"},
  {"c.template.key": new ArrayBuffer(10)},
  {"key":"value"},
  {"another key": "another value"}
]
await new Promise((resolve)=>{
  addon.cache("a.template", resolve) //1)dangerous under load (either put mutex or forbid it); 2)works AFTER setSyntax
});
addon.clearCache("a.template"); // dangerous under load; 
addon.maxChunk(64*1024); //dangerous under load
addon.streamToJS({
  name: "a.template",
  id: 0,
  other: {
    "insert-key": [{
      name: "b.template",
      id: 1
    },{
      name: "d.template",
      id: 2
    }],
    "interpolation-key-2": [{
      name: "c.template",
      id: 3,
      other: {
        "one-more-component": [{
          name: "component.template",
          id: 4
        }]
      }
    }]
  }
}, (data: (ArrayBuffer|[number, string])[], release: ()=>void)=>{
  if(!data) {
    // end response in uWS.
    return;
  }

  for(let el of data){
    if(el instanceof ArrayBuffer){
      // write el to response in uWS
    } else {
      var [id, key] = el;
      var str = insertData[id][key]
      // write str to response in uWS
    }
  }
  release();
});
// C++ has 2 vectors: for js and for fs. if !removeOn - syntax goes only to js.
addon.setSyntax(".html", {
  prefix: "<!--TEMPLATE",
  insertOn: '=',
  removeOn: undefined, // or _REMOVE= or whatever
  end: "-->"
}, 30); /*30 - max size of insert key*/
// C++ for js will use start: {{= BY CONCATENATION
