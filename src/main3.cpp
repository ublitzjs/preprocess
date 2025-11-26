#include "./include/shared3.hpp"
#include <napi.h>
namespace exports {
  void streamToFS(const Napi::CallbackInfo &info){}
  void streamToJS(const Napi::CallbackInfo &info){}
  void setSyntax(const Napi::CallbackInfo &info){}
  void setMaxChunk(const Napi::CallbackInfo &info){}
  void silentCache(const Napi::CallbackInfo &info){}
  void compile(const Napi::CallbackInfo &info){}
  void clearCache(const Napi::CallbackInfo &info){}
}
Napi::Object Init(Napi::Env env, Napi::Object exportsObj){
  exportsObj.Set("streamToFS", Napi::Function::New(env, exports::streamToFS));
  exportsObj.Set("streamToJS", Napi::Function::New(env, exports::streamToJS));
  exportsObj.Set("setSyntax", Napi::Function::New(env, exports::setSyntax));
  exportsObj.Set("setMaxChunk", Napi::Function::New(env, exports::setMaxChunk));
  exportsObj.Set("silentCache", Napi::Function::New(env, exports::silentCache));
  exportsObj.Set("compile", Napi::Function::New(env, exports::compile));
  exportsObj.Set("clearCache", Napi::Function::New(env, exports::clearCache));
  return exportsObj;
}
NODE_API_MODULE(addon, Init);

