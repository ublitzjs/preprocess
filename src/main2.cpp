#include <map>
#include <vector>
#include <mutex>
#include "./include/shared2.hpp"
#include <napi.h>
#include <uv.h>

Napi::ThreadSafeFunction caching::emitter;
namespace exports {
  void Stop(const Napi::CallbackInfo& info){
    caching::emitter.Release();
    streaming::workers.Stop();
    caching::workers.Stop();
  } 
  void streamToFS(const Napi::CallbackInfo &info){
    // I have to open input file here. If no file or too many descriptors open - throw js error;
    Napi::ObjectReference params(Napi::Persistent(info[0].As<Napi::Object>()));
    Napi::ObjectReference files(Napi::Persistent(info[1].As<Napi::Object>()));
    //std::thread(workers::FS::ThreadPool).detach();
  }
  void streamToJS(const Napi::CallbackInfo &info){

  }
  void createThreadPools(const Napi::CallbackInfo &info){
    streaming::workers.Init(info[0].As<Napi::Number>().Uint32Value());
    caching::workers.Init(info[1].As<Napi::Number>().Uint32Value());
  }
  void setSyntax(const Napi::CallbackInfo &info){
    Napi::Object params = info[1].As<Napi::Object>();
    syntax::dataVector.emplace_back(
        info[0].As<Napi::String>().Utf8Value(),
        params.Get("prefix").As<Napi::String>().Utf8Value(),
        params.Get("insertOn").As<Napi::String>().Utf8Value(),
        params.Get("removeOn").As<Napi::String>().Utf8Value(),
        params.Get("end").As<Napi::String>().Utf8Value(),
        info[2].As<Napi::Number>().Int32Value()
    );
  }
  void maxChunk(const Napi::CallbackInfo &info){
    maxChunkSize = info[0].As<Napi::Number>().Int32Value();
  }
  void setCachingEmitter(const Napi::CallbackInfo& info){
    caching::emitter = Napi::ThreadSafeFunction::New(
       info.Env(),
       info[0].As<Napi::Function>(),
       "caching finalizing emitter",
       0,
       1
     );
  }
  Napi::Boolean cache(const Napi::CallbackInfo &info){
    const std::string templateName = info[0].As<Napi::String>().Utf8Value();
    caching::dataStruct* cachePointer;
    std::atomic<bool>* pendingReady = nullptr;
    {
      std::lock_guard<std::mutex> lock(caching::dataMapMutex);
      if(caching::dataMap.count(templateName)){
        caching::dataStruct& cache = caching::dataMap[templateName];
        cache.tmp = false;
        cache.busyLevel++;
        if(cache.isReady.load())
          return Napi::Boolean::New(info.Env(), true);
        
        pendingReady = &cache.isReady;
      } else {
        auto [keyValuePair, boolean] = caching::dataMap.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(templateName.c_str()),
            std::forward_as_tuple()
        );
        cachePointer = &keyValuePair->second;
        cachePointer->busyLevel++;
        keyValuePair->second.MarkAsCachedInMap(&keyValuePair->first);
      }
    }
    if(pendingReady) return Napi::Boolean::New(info.Env(), false);
    syntax::dataStruct* syntaxStruct = syntax::findMatchingStruct(templateName);
    if(!syntaxStruct) {
      Napi::Error::New(
        info.Env(), "Pattern for this template was not found"
      ).ThrowAsJavaScriptException();
      return Napi::Boolean::New(info.Env(), false);
    }
    caching::workers.enqueue([cachePointer, syntaxStruct, templateName] {
      libuvWorker::JSCachingThreadPool(
          true,
          true,
          cachePointer,
          syntaxStruct,
          templateName
      );
    });
    return Napi::Boolean::New(info.Env(), false);
  };
  Napi::Boolean clearCache(const Napi::CallbackInfo &info){
    const std::string& templateName = info[0].As<Napi::String>().Utf8Value();
    std::lock_guard<std::mutex> lock(caching::dataMapMutex);
    if(!caching::dataMap.count(templateName)) 
      return Napi::Boolean::New(info.Env(), true);
    caching::dataStruct& cache = caching::dataMap[templateName];
    // remove that 1 additional level when calling "cache" with "non-temporary" state.
    if(cache.busyLevel) cache.busyLevel--;
    cache.tmp = true;
    bool isFree = !cache.busyLevel;
    if(isFree)
      caching::dataMap.erase(info[0].As<Napi::String>().Utf8Value()); //destructor takes care of heap memory
    return Napi::Boolean::New(info.Env(), isFree);
  }
}
Napi::Object Init(Napi::Env env, Napi::Object exportsObj){
  exportsObj.Set("createThreadPools", Napi::Function::New(env, exports::createThreadPools));
  exportsObj.Set("streamToFS", Napi::Function::New(env, exports::streamToFS));
  exportsObj.Set("streamToJS", Napi::Function::New(env, exports::streamToJS));
  exportsObj.Set("setSyntax", Napi::Function::New(env, exports::setSyntax));
  exportsObj.Set("maxChunk", Napi::Function::New(env, exports::maxChunk));
  exportsObj.Set("cache", Napi::Function::New(env, exports::cache));
  exportsObj.Set("setCachingEmitter", Napi::Function::New(env, exports::setCachingEmitter));
  exportsObj.Set("clearCache", Napi::Function::New(env, exports::clearCache));
  exportsObj.Set("Stop", Napi::Function::New(env, exports::Stop));
  return exportsObj;
}
NODE_API_MODULE(addon, Init);
