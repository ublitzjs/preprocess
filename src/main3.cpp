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
  Napi::Value silentCache(const Napi::CallbackInfo &info){
    std::string str = std::move(info[0].As<Napi::String>().Utf8Value());
    auto it = caching::dataMap.find(str);
    if(it != caching::dataMap.end()) {
      it->second->mutex.lock();
      Status status = it->second->getStatus();
      if(status < 0) {
        bool isFree = it->second->getBusyLevel(); // if error in libuv
        it->second->mutex.unlock();
        if(isFree) {delete it->second; caching::dataMap.erase(str);}
        return Napi::Number::New(info.Env(), status); 
      } else it->second->book();
      return info.Env().Undefined();
    };
    caching::data* cache = new caching::data(str, false, info[1].As<Napi::Boolean>().Value());
    // must be even empty - sacrifice memory to reduce caching::statusMutex lock time.
    cache->waitingTasks = new std::vector<streaming::data::MinBase*>();
    uv_work_t* request = new uv_work_t();
    request->data = cache;
    uvWorkers::forSilentCache* worker = new uvWorkers::forSilentCache(info.Env());
    worker->Queue();
    return info.Env().Undefined();
  }
  void compile(const Napi::CallbackInfo &info){}
  void silentClearCache(const Napi::CallbackInfo &info){
    std::string str = std::move(info[0].As<Napi::String>().Utf8Value());
    auto it = caching::dataMap.find(str);
    if(it == caching::dataMap.end()) return;
    it->second->mutex.lock();
    if(!it->second->unbookAndCheckIfFree() || it->second->getStatus() == Status::PendingDiskRead)  return it->second->mutex.unlock();
    it->second->mutex.unlock();
    delete it->second;
    caching::dataMap.erase(str);
  }
}
Napi::Object Init(Napi::Env env, Napi::Object exportsObj){
  exportsObj.Set("streamToFS", Napi::Function::New(env, exports::streamToFS));
  exportsObj.Set("streamToJS", Napi::Function::New(env, exports::streamToJS));
  exportsObj.Set("silentCache", Napi::Function::New(env, exports::silentCache));
  exportsObj.Set("init", Napi::Function::New(env, exports::init));
  exportsObj.Set("compile", Napi::Function::New(env, exports::compile));
  exportsObj.Set("silentClearCache", Napi::Function::New(env, exports::silentClearCache));
  return exportsObj;
}
NODE_API_MODULE(addon, Init);

