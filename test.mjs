#!/usr/bin/env node
import {exec} from "node:child_process";
import { existsSync } from "node:fs";
import {arch, platform} from "node:os"
import {argv, argv0} from "node:process";
const wantedExe = "./"+platform()+"-"+arch()+".exe";
if(!existsSync(wantedExe)) {
  console.log("using fallback js implementation, because there is no right binary for " + wantedExe + " yet");
} else {argv.splice(0,2);
  exec(wantedExe + " " + argv.join(" "), (err,stdout, stderr)=>{
    if(err) console.error("ERR: ", err, stderr);
    else console.log("successful execution: ", stdout);
  });
}
