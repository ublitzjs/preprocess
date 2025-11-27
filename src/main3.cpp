#include "./include/shared3.hpp"
#include <napi.h>
namespace exports {
  void streamToFS(const Napi::CallbackInfo &info){}
  void streamToJS(const Napi::CallbackInfo &info){}
  void init(const Napi::CallbackInfo &info){
    maxChunkSize = info[0].As<Napi::Number>().Uint32Value();
    Napi::Array params = info[1].As<Napi::Array>();
    syntax::dataVector.reserve(params.Length());
    for(uint8_t i = 0; i < params.Length(); i++){
      Napi::Object newSyntax = params[i].AsValue().As<Napi::Object>();
      syntax::dataVector.emplace_back(
          newSyntax.Get("pattern").As<Napi::String>().Utf8Value(),
          newSyntax.Get("prefix").As<Napi::String>().Utf8Value(),
          newSyntax.Get("insertOn").As<Napi::String>().Utf8Value(),
          newSyntax.Get("removeOn").As<Napi::String>().Utf8Value(),
          newSyntax.Get("end").As<Napi::String>().Utf8Value(),
          newSyntax.Get("maxInsertKeyLength").As<Napi::Number>().Int32Value()
      );
    } 
  }
  void silentCache(const Napi::CallbackInfo &info){}
  void compile(const Napi::CallbackInfo &info){}
  void clearCache(const Napi::CallbackInfo &info){}
}
Napi::Object Init(Napi::Env env, Napi::Object exportsObj){
  exportsObj.Set("streamToFS", Napi::Function::New(env, exports::streamToFS));
  exportsObj.Set("streamToJS", Napi::Function::New(env, exports::streamToJS));
  exportsObj.Set("silentCache", Napi::Function::New(env, exports::silentCache));
  exportsObj.Set("init", Napi::Function::New(env, exports::init));
  exportsObj.Set("compile", Napi::Function::New(env, exports::compile));
  exportsObj.Set("clearCache", Napi::Function::New(env, exports::clearCache));
  return exportsObj;
}
NODE_API_MODULE(addon, Init);

