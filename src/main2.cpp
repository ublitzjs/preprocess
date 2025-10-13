#include <future>
#include <atomic>
#include <thread>
#include <napi.h>
#include <map>
#include <vector>
#include <mutex>
#include <regex>
#include <uv.h>
#include "./include/os.hpp"
#include "./include/shared2.hpp"

std::mutex caches::mutex;
std::vector<PatternStruct> *patterns = new std::vector<PatternStruct>();
std::map<std::string, caches::dataStruct> caches::map;
uint32_t maxChunkSize = 64*1024;

namespace helpers {
  inline PatternStruct* findPattern(const std::string& patternString){
    for(PatternStruct& patternStruct : *patterns){
      std::cmatch matches;
      if(
        std::regex_search(
          patternString.data(),
          matches,
          patternStruct.pattern
        )
      ) return &patternStruct;
    }
    return nullptr;
  }
}
namespace exports {
  void streamToFS(const Napi::CallbackInfo &info){
    // I have to open input file here. If no file or too many descriptors open - throw js error;
    Napi::ObjectReference ref(Napi::Persistent(info[0].As<Napi::Object>()));
    //std::thread(workers::FS::ThreadPool).detach();
  }
  void streamToJS(const Napi::CallbackInfo &info){

  }
  void setSyntax(const Napi::CallbackInfo &info){
    const std::string& patternString = info[0].As<Napi::String>().Utf8Value();
    Napi::Object params = info[1].As<Napi::Object>();
    uint8_t maxInsertKeyLength = info[2].As<Napi::Number>().Int32Value();
    const std::string& prefix = params.Get("prefix").As<Napi::String>().Utf8Value();
    const std::string& insertOn = params.Get("insertOn").As<Napi::String>().Utf8Value();
    const std::string prefixedInsertOn = prefix + insertOn;
    const std::string& removeOn = params.Get("removeOn").As<Napi::String>().Utf8Value();
    const std::string& end = params.Get("end").As<Napi::String>().Utf8Value();
    const uint8_t maxParamLength = std::max({ insertOn.length(), removeOn.length() });
    const uint8_t maxSyntaxPartialsSizeVar = std::max<int16_t>({
        static_cast<uint8_t>(maxInsertKeyLength + end.size()) ,
        static_cast<uint8_t>(prefix.size() + maxParamLength),
    }) - 1 /*because PARTIALS*/;
    patterns->emplace_back(
        std::regex(patternString),
        prefix,
        insertOn,
        prefixedInsertOn,
        removeOn,
        end,
        maxParamLength,
        maxSyntaxPartialsSizeVar
    );
  }
  void maxChunk(const Napi::CallbackInfo &info){
    maxChunkSize = info[0].As<Napi::Number>().Int32Value();
  }
  struct CacheWorkerData {
    enum Statuses {
      Success = (0),
      NoFile = (1),
      CantGetSize = (2),
      CantRead = (3),
      CantAllocate = (4)
    };
    uv_work_t uv_request; // when instance get deleted - request data as well;
    PatternStruct* pattern;
    std::string templateName;
    std::promise<Statuses> sync;
    bool cacheIsTmp;
    CacheWorkerData(bool cacheIsTmp, PatternStruct* patternStruct, std::string templateName)
      :pattern(patternStruct), templateName(std::move(templateName)), cacheIsTmp(cacheIsTmp) {}
    static void ThreadPool(bool tmp, PatternStruct* patternStruct, std::string templateName, Napi::ThreadSafeFunction tsfn){
      CacheWorkerData* workerData = new CacheWorkerData(tmp, patternStruct, templateName); // in case main thread gets interrupted - this doesn't break;
      std::future<CacheWorkerData::Statuses> await =
          workerData->sync.get_future();
      workerData->uv_request.data = workerData;
      uv_queue_work(uv_default_loop(), &workerData->uv_request,
                    CacheWorkerData::ExecuteWork, CacheWorkerData::Finalize);
      await.wait();
      tsfn.BlockingCall([status = await.get(), templateName](
        Napi::Env env, Napi::Function jsCallback) {
        switch (status) {
        case CacheWorkerData::NoFile:
        case CacheWorkerData::CantRead:
        case CacheWorkerData::CantGetSize:
          return Napi::Error::New(env, "Can't find file " + templateName)
              .ThrowAsJavaScriptException();
        case CacheWorkerData::CantAllocate:
          return Napi::Error::New(env, "Can't allocate memory for this file: " +
                                           templateName)
              .ThrowAsJavaScriptException();
        case CacheWorkerData::Success:
          jsCallback.Call({});
          return;
        }
      });
      tsfn.Release();
      delete workerData;
    }
    static void ExecuteWork(uv_work_t *req){
      CacheWorkerData& workerData = *static_cast<CacheWorkerData *>(req->data);
      int descriptor = open(workerData.templateName.c_str(), O_RDONLY);
      if (descriptor == -1) {
        return workerData.sync.set_value(NoFile);
      }
      // get file's size
      struct stat inputStats;
      if (fstat(descriptor, &inputStats)) {
        close(descriptor);
        return workerData.sync.set_value(CantGetSize);
      }
      uint8_t syntaxPartialsSize = workerData.pattern->syntaxPartialsSize;
      char* const wholeChunk = new char[
         syntaxPartialsSize + inputStats.st_size
      ];
      if (!wholeChunk){
        close(descriptor);
        return workerData.sync.set_value(CantAllocate);
      }
      if (read(descriptor, wholeChunk + syntaxPartialsSize, inputStats.st_size) == -1) {
        delete[] wholeChunk;
        close(descriptor);
        return workerData.sync.set_value(CantRead);
      }
      close(descriptor);
      {
        std::lock_guard<std::mutex> lock(caches::mutex);
        caches::map[workerData.templateName].Init(workerData.cacheIsTmp, wholeChunk, syntaxPartialsSize, inputStats.st_size);
      }

      workerData.sync.set_value(Success);
    }
    static void Finalize(uv_work_t *req, int status){
      
    }
  };
  Napi::Boolean cache(const Napi::CallbackInfo &info){
    const std::string& templateName = info[0].As<Napi::String>().Utf8Value();
    bool cacheIsTmp = info[1].As<Napi::Boolean>().Value();
    bool* isReadyPointer = nullptr;
    {
      std::lock_guard<std::mutex> lock(caches::mutex);
      if(caches::map.count(templateName)){
        caches::dataStruct& cache = caches::map[templateName];
        if(cache.tmp) cache.tmp = cacheIsTmp;

        if(!cacheIsTmp) {
          // busy until next "clearCache" call
          cache.busyLevel++;
        }

        if(cache.isReady){
          return Napi::Boolean::New(info.Env(), false);
        }
        isReadyPointer = &cache.isReady;
      }
    }
    Napi::Function jsCallback = info[2].As<Napi::Function>();
    if(isReadyPointer){
      std::atomic_ref<bool> ref(*isReadyPointer);
      ref.wait(false, std::memory_order_relaxed);
      jsCallback.Call({});
      return Napi::Boolean::New(info.Env(), false);
    }
    PatternStruct* patternStruct = helpers::findPattern(templateName);
    if(!patternStruct) {
      Napi::Error::New(
        info.Env(), "Pattern for this template was not found"
      ).ThrowAsJavaScriptException();
      return Napi::Boolean::New(info.Env(), true);
    }
    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
        info.Env(),
        jsCallback,
        "Caching attempt",
        0,
        1
    );
    std::thread(CacheWorkerData::ThreadPool, cacheIsTmp, patternStruct, templateName, tsfn).detach();
    return Napi::Boolean::New(info.Env(), true);
  };
  Napi::Boolean clearCache(const Napi::CallbackInfo &info){
    std::lock_guard<std::mutex> lock(caches::mutex);
    const std::string& templateName = info[0].As<Napi::String>().Utf8Value();
    if(!caches::map.count(templateName)) return Napi::Boolean::New(info.Env(), true);
    caches::dataStruct& cache = caches::map[templateName];
    // remove that 1 additional level when calling "cache" with "non-temporary" state.
    if(cache.busyLevel) cache.busyLevel--;
    bool isFree = !cache.busyLevel;
    if(isFree) {
      caches::map.erase(info[0].As<Napi::String>().Utf8Value()); //destructor takes care of heap memory
    } else {
      cache.tmp = true;
    }
    return Napi::Boolean::New(info.Env(), isFree);
  }
 // type FSStreamParams = {
  //     name: string;
 //      keys: Record<string, string | FSStreamParams[]>
//     }
 // streamToFS(params: FSStreamParams, output:string, callback: ()=>void): void;
 // streamToJS(params: JSStreamParams, callback: JSStreamCb): void;
 // setSyntax(pattern: string, params: {
 //   prefix: string;
 //   insertOn: string;
 //   removeOn?: string;
 //   end: string;
 // }, maxInsertKeyLength: number): void;
 // maxChunk(size: number): void;
 // clearCache(name: string): void;
 // cache(name: string, cb: ()=>void): void;
}
Napi::Object Init(Napi::Env env, Napi::Object exportsObj){
  patterns->reserve(3);
  exportsObj.Set("streamToFS", Napi::Function::New(env, exports::streamToFS));
  exportsObj.Set("streamToJS", Napi::Function::New(env, exports::streamToJS));
  exportsObj.Set("setSyntax", Napi::Function::New(env, exports::setSyntax));
  exportsObj.Set("maxChunk", Napi::Function::New(env, exports::maxChunk));
  exportsObj.Set("cache", Napi::Function::New(env, exports::cache));
  exportsObj.Set("clearCache", Napi::Function::New(env, exports::clearCache));
  return exportsObj;
}
NODE_API_MODULE(addon, Init);
