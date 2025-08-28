#!/usr/bin/env node
import {execSync} from "node:child_process";
import {existsSync} from "node:fs";
import {log} from "node:console"
import {arch, platform} from "node:os"
import {argv} from "node:process";
import startFallback from "./fallback/main.mjs";
const HERE = import.meta.dirname + "/"
const wantedExe = HERE+platform()+"-"+arch()+".bin";
argv.splice(0,2)
if(!existsSync(wantedExe)) {
  log("Currently there is no "+wantedExe+" for your platform");
  //startFallback(argv);
} else {
  log(
    execSync(
      wantedExe + " " + argv.map(
        (arg)=>"\""+arg+"\""
      ).join(" ")
    ).toString()
  );
}
