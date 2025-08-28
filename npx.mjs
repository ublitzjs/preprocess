#!/usr/bin/env node
import{execSync as i}from"node:child_process";import{existsSync as o}from"node:fs";import{log as t}from"node:console";import{arch as n,platform as a}from"node:os";import{argv as r}from"node:process";var l=import.meta.dirname+"/",e=l+a()+"-"+n()+".bin";r.splice(0,2);o(e)?t(i(e+" "+r.map(s=>'"'+s+'"').join(" ")).toString()):t("Currently there is no "+e+" for your platform");
