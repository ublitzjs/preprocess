import {buildSync} from "esbuild";
buildSync({
  minify: true,
  platform: "node",
  format: "esm",
  bundle: true,
  outfile: "./npx.mjs",
  entryPoints: ["./src/npx.mjs"]
})
