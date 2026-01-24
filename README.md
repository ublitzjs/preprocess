# @ublitzjs/preprocess library for preprocessing code/templates
It has two modules: @ublitzjs/preprocess itself and @ublitzjs/preprocess/render.  
## The first one exports function "preprocess", which removes any data from any file within two strings including them.  
When using "/\*!REMOVE\*/" and /\*!STOP_REMOVE\*/ (use set what you need) code transforms from this:  
```typescript
console.log(
    "hello world",
    /*!REMOVE*/ "This is for debug",
    //this is commented functionality /*!STOP_REMOVE*/ "this was commented"
);
```
To this:
```typescript
console.log(
    "hello world",
 "this was commented"
)
```
To implements build script it is enough to:
```typescript
import {preprocess} from "@ublitzjs/preprocess";
await preprocess("input.js", "output.js", "/*!REMOVE*/", "/*!STOP_REMOVE*/")
```
When using with esbuild (which bundles code in one "executable") comments must start from "!" and option "legalComments" must be "inline".  
## The other one - a template engine (more a tokenizer with unique design);
It can handle only interpolation, but in a streaming way + recursive interpolation of templates + multiple interpolations in one place. It is great for generating pages from components. Soon there will be examples (it is more of a framework, so get ready for difficulties)
