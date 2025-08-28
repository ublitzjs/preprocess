import {platform} from "node:os";
import {exec} from "node:child_process";
import {log} from "node:console";
var compiler = platform() == "darwin" ? "clang++" : "g++";
var platformApproach = platform() == "win32" ? 0 : 1;
exec(compiler + " -std=c++17 -s -O3 -o " + "./binary.exe -DPLATFORM_APPROACH=" + platformApproach + " -I./StringZilla/include src/main.cpp src/worker.cpp", (err, stdout, stderr)=>{
  log("ERR", err);
  log("stdout: ", stdout);
  log("stderr: ", stderr);
})


