#include <vector>
#include "./include/shared2.hpp"
#include <napi.h>
#include <uv.h>
#include <tbb/concurrent_hash_map.h>

namespace exports {
  void Stop(const Napi::CallbackInfo& info){
    caching::emitter.Release();
    streaming::workers.Stop();
  } 
  void streamToFS(const Napi::CallbackInfo &info){
    Napi::Object params(info[0].As<Napi::Object>());
    Napi::Array inputFiles(info[1].As<Napi::Array>());
    const std::string& mainTemplate = inputFiles.Get(params.Get("id").As<Napi::Number>().Uint32Value()).As<Napi::String>().Utf8Value();
    const syntax::dataStruct* const syntaxStruct = syntax::findMatchingStruct(mainTemplate);
    if(!syntaxStruct){
      //TODO
    }
    caching::data::perhaps_streamed* recentlyCreatedCachePointer;
    cross_os::descriptor_t output = cross_os::OpenFileWrite(mainTemplate.data());
    if(output == cross_os::invalid_descriptor_t) {
      return Napi::Error::New(info.Env(), "output file couldn't be created").ThrowAsJavaScriptException();
    }

    {
      caching::dataMapType::accessor accessor;
      if(!caching::dataMap.insert(accessor, mainTemplate)){
        // cache has failed
        if(accessor->second->status < 0) return Napi::Error::New(info.Env(), "outer template failed to be cached").ThrowAsJavaScriptException();
        streaming::data::forFS* task = new streaming::data::forFS(
            output,
            params,
            inputFiles,
            info.Env(),
            info[3].As<Napi::Function>(),
            syntaxStruct,
            accessor->second
        );
        // cache is already usable
        if(accessor->second->status) return streaming::workers.enqueue([task]{task->threadCB();});
        // cache is pending
        else return streaming::cacheDependentTasks[accessor->second].push_back(task);
      }
      recentlyCreatedCachePointer = accessor->second;
    }
    caching::libuv::dataStruct* workerData = new caching::libuv::dataStruct(syntaxStruct, recentlyCreatedCachePointer, mainTemplate, true);
    workerData->uv_request.data = workerData;
    caching::libuv::enqueue(&workerData->uv_request);
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
    const bool waitForAST = info[1].As<Napi::Boolean>().Value();
    caching::data::full* cachePointer;
    {
      caching::dataMapType::accessor accessor;
      bool cacheWasRecentlyCreated = caching::dataMap.insert(accessor, templateName);

      if(cacheWasRecentlyCreated) {
        accessor->second = (cachePointer =  new caching::data::full{templateName, waitForAST});
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
