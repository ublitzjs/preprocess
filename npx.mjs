#!/usr/bin/env node
import {exec} from "node:child_process";
import { existsSync } from "node:fs";
import {arch, platform} from "node:os"
import {argv} from "node:process";
const HERE = import.meta.dirname + "/"
const wantedExe = HERE+platform()+"-"+arch()+".exe";
if(!existsSync(wantedExe)) {
  console.log("Currently there is no "+wantedExe+" for your platform");
} else {
  argv.splice(0,2);
  exec(wantedExe + " " + argv.map((arg)=>"\""+arg+"\"").join(" "), (err,stdout, stderr)=>{
    if(err) console.error("Exe error:\n",stderr);
    else console.log("Exe success:\n", stdout);
  });
}
