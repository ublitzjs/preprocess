#include <vector>
#include "./include/shared2.hpp"
#include <napi.h>
#include <uv.h>
#include <tbb/concurrent_hash_map.h>

Napi::ThreadSafeFunction caching::emitter;
namespace exports {
  void Stop(const Napi::CallbackInfo& info){
    caching::emitter.Release();
    streaming::workers.Stop();
  } 
  void streamToFS(const Napi::CallbackInfo &info){
    // I have to open input file here. If no file or too many descriptors open - throw js error;
    Napi::ObjectReference params(Napi::Persistent(info[0].As<Napi::Object>()));
    Napi::ObjectReference files(Napi::Persistent(info[1].As<Napi::Object>()));
    files.Get(0u).As<Napi::Number>().Uint32Value();
    //std::thread(workers::FS::ThreadPool).detach();
  }
  void streamToJS(const Napi::CallbackInfo &info){

  }
  void createThreadPools(const Napi::CallbackInfo &info){
    streaming::workers.Init(info[0].As<Napi::Number>().Uint32Value());
  }
  void setSyntax(const Napi::CallbackInfo &info){
    Napi::Array params = info[0].As<Napi::Array>();
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
    const std::string& templateName = info[0].As<Napi::String>().Utf8Value();
    caching::dataStruct* cachePointer;
    {
      caching::dataMapType::accessor accessor;
      bool cacheWasRecentlyCreated = caching::dataMap.insert(accessor, templateName);

      if(cacheWasRecentlyCreated) {
        cachePointer = (accessor->second = new caching::dataStruct{templateName});
      } else {
        accessor->second->busyLevel++;
        return Napi::Boolean::New(info.Env(), accessor->second->status);
      }
    }
    const syntax::dataStruct* const syntaxStruct = syntax::findMatchingStruct(templateName);
    // if developer pays attention to files he passes, it is a very rare situation.
    if(!syntaxStruct) {
      Napi::Error::New(
        info.Env(), "Pattern for this template was not found"
      ).ThrowAsJavaScriptException();
      { 
        caching::dataMapType::accessor accessor;
        caching::dataMap.find(accessor, templateName);
        if(--accessor->second->busyLevel){
          streaming::enqueueCacheDependentTasks(accessor->second);
        } else {
          delete cachePointer;
          caching::dataMap.erase(accessor);
        }
      }
      return Napi::Boolean::New(info.Env(), false);
    }
    caching::libuv::dataStruct* workerData = new caching::libuv::dataStruct(syntaxStruct, cachePointer, templateName, true);
    workerData->uv_request.data = workerData;
    caching::libuv::enqueue(&workerData->uv_request);
    return Napi::Boolean::New(info.Env(), false);
  };
  // this function is expected to be called ONLY if template was cache with "cache" function above
  Napi::Boolean clearCache(const Napi::CallbackInfo &info){
    const std::string& templateName = info[0].As<Napi::String>().Utf8Value();
    caching::dataMapType::accessor accessor;
    bool found = caching::dataMap.find(accessor, templateName);
    if(!found) return Napi::Boolean::New(info.Env(), true);
    bool shouldBefreed = !(accessor->second->busyLevel ? --accessor->second->busyLevel : accessor->second->busyLevel);
    if(shouldBefreed){
      delete accessor->second;
      caching::dataMap.erase(accessor); //destructor takes care of heap memory
    }
    return Napi::Boolean::New(info.Env(), shouldBefreed);
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
