#include "./include/shared3.hpp"
#include <napi.h>

namespace exports {
  void streamToFS(const Napi::CallbackInfo &info){
    processing::tasks::forFS* task;
    Napi::Env env = info.Env();
    {
      // ( (erroredCache: [string, Status], data: null)=>void ) | ( (erroredTemplate: null, data: any)=>void ) 
      Napi::Function cb = info[3].As<Napi::Function>();
      task = new processing::tasks::forFS(cb, info[0].As<Napi::Object>(), info[1].As<Napi::Array>());
      
      //inclusions.emplace_back(descriptor, cacheStruct, cacheIsReady, fileSize, instructions);
      Napi::Object instructions = info[0].As<Napi::Object>();
      Napi::Array templatesList = info[1].As<Napi::Array>();
      uint8_t index = instructions.Get("id").As<Napi::Number>().Uint32Value();
      {
        const std::string output = info[2].As<Napi::String>().Utf8Value().data();
        if( (task->output = cross_os::OpenFileWrite(output.data())) == cross_os::invalid_descriptor){
          delete task;
          Napi::Array message = Napi::Array::New(env, 2u);
          message.Set(0u, output);
          message.Set(1u, Napi::Number::New(env, Status::CantUseFile));
          cb.Call({message});
          return;
        }
      }
      //[NAME, HAS AST, USAGES]
      Napi::Array templateDirections = templatesList.Get(index).As<Napi::Array>();
      //templatesList.Set(index, env.Undefined());
      task->bookedCaches.getUsages(index, templatesList.Length()) =
        templateDirections.Has(2u) ? templatesList.Get(2u).As<Napi::Number>().Int32Value() : -1;
      Napi::String jsTemplateName =  templateDirections.Get(0u).As<Napi::String>();
      const std::string templateName = jsTemplateName.Utf8Value();
      auto it = cache::dataMap.find(templateName);
      processing::state& stateStruct = task->inclusions.emplace_back(instructions);
      cache*& cacheStruct = task->bookedCaches.getPtr(index);
      if(it != cache::dataMap.end()){
        Status status;
        it->second->mutex.lock();
        if( !(status = cacheStruct->getStatus()) ) {
          it->second->waitingTasks->push_back(task);
          return it->second->mutex.unlock();
        }
        it->second->mutex.unlock();

        if(status < 0) {
          delete task;
          Napi::Array message = Napi::Array::New(env, 2u);
          message.Set(0u, jsTemplateName);
          message.Set(1u, Napi::Number::New(env, status));
          cb.Call({message});
          return;
        }
        else /*ok*/ {
          stateStruct.initForReadyCache(it->second->size);
          cacheStruct = it->second;
        }
      } else {
        if(
            (stateStruct.descriptor = cross_os::OpenFileRead(templateName.data())) == cross_os::invalid_descriptor
            || (stateStruct.fileSize = cross_os::GetFileSize(stateStruct.descriptor)) == cross_os::invalid_file_size
        ){
          cross_os::CloseDescriptor(stateStruct.descriptor);
          cross_os::CloseDescriptor(task->output);
          delete task;
          Napi::Array message = Napi::Array::New(env, 2u);
          message.Set(0u, jsTemplateName);
          message.Set(1u, Napi::Number::New(env, Status::CantUseFile));
          cb.Call({message});
          return;
        }
        bool isGlobal = stateStruct.fileSize <= maxChunkSize;
        cacheStruct = new cache(
            templateName,
            templateDirections.Has(1u) ? templateDirections.Get(1u) : false,
            isGlobal
            );
        uvWorkers::forProcessing* worker = new uvWorkers::forProcessing(env, &stateStruct, cacheStruct);
        if(isGlobal){
          cache::dataMap[templateName] = cacheStruct;
          cacheStruct->waitingTasks->push_back(task);
          worker->waitingTasks = cacheStruct->waitingTasks;
        } else {
          worker->task = task;
        }
        return worker->Queue();
      }
      // here cache exists and is ready, stateStruct as well
    }
    task->processAndCheckIfFinished(env);
  }
  void streamToJS(const Napi::CallbackInfo &info){
    processing::tasks::forJS* task;
    Napi::Env env = info.Env();
    {
      // ( (erroredCache: [string, Status], data: null)=>void ) | ( (erroredTemplate: null, data: any)=>void ) 
      Napi::Function cb = info[3].As<Napi::Function>();
      task = new processing::tasks::forJS(cb, info[0].As<Napi::Object>(), info[1].As<Napi::Array>(), env);
      
      Napi::Object instructions = info[0].As<Napi::Object>();
      Napi::Array templatesList = info[1].As<Napi::Array>();
      uint8_t index = instructions.Get("id").As<Napi::Number>().Uint32Value();
      processing::state& stateStruct = task->inclusions.emplace_back(instructions);
      cache*& cacheStruct = task->bookedCaches.getPtr(index);
      {
        //[NAME, HAS AST, USAGES]
        Napi::Array templateDirections = templatesList.Get(index).As<Napi::Array>();
        //templatesList.Set(index, env.Undefined());
        task->bookedCaches.getUsages(index, templatesList.Length()) =
          templateDirections.Has(2u) ? templatesList.Get(2u).As<Napi::Number>().Int32Value() : -1;
        Napi::String jsTemplateName =  templateDirections.Get(0u).As<Napi::String>();
        const std::string templateName = jsTemplateName.Utf8Value();
        auto it = cache::dataMap.find(templateName);
        if(it != cache::dataMap.end()){
          stateStruct.initForReadyCache(it->second->size);
          cacheStruct = it->second;
        } else {
          if(
              (stateStruct.descriptor = cross_os::OpenFileRead(templateName.data())) == cross_os::invalid_descriptor
              || (stateStruct.fileSize = cross_os::GetFileSize(stateStruct.descriptor)) == cross_os::invalid_file_size
          ){
            cross_os::CloseDescriptor(stateStruct.descriptor);
            delete task;
            Napi::Array message = Napi::Array::New(env, 2u);
            message.Set(0u, jsTemplateName);
            message.Set(1u, Napi::Number::New(env, Status::CantUseFile));
            cb.Call({message});
            return;
          }
          bool isGlobal = stateStruct.fileSize <= maxChunkSize;
          cacheStruct = new cache(
              templateName,
              templateDirections.Has(1u) ? templateDirections.Get(1u) : false,
              isGlobal
              );
          uvWorkers::forProcessing* worker = new uvWorkers::forProcessing(env, &stateStruct, cacheStruct);
          if(isGlobal){
            worker->waitingTasks = cacheStruct->waitingTasks;
            cache::dataMap[templateName] = cacheStruct;
            cacheStruct->waitingTasks->push_back(task);
          } else {
            worker->task = task;
          }
          return worker->Queue();
        }
        // here cache exists and is ready, stateStruct as well
      }
    }
    task->processAndCheckIfFinished(env);
  }
  void init(const Napi::CallbackInfo &info){
    Napi::Array instructions = info[0].As<Napi::Array>();
    for(Napi::Object obj : instructions){}


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
  // silentCache(templatePath: string, hasASTInSource: boolean): void
  Napi::Value silentCache(const Napi::CallbackInfo &info){
    std::string str = std::move(info[0].As<Napi::String>().Utf8Value());
    auto it = cache::dataMap.find(str);
    if(it != cache::dataMap.end()) {
      it->second->mutex.lock();
      Status status = it->second->getStatus();
      if(status < 0) {
        bool isFree = it->second->getBusyLevel(); // if error in libuv
        it->second->mutex.unlock();
        if(isFree) {delete it->second; cache::dataMap.erase(str);}
        return Napi::Number::New(info.Env(), status); 
      } else it->second->book();
      return info.Env().Undefined();
    };
    cache* cacheStruct = new cache(str, info[1].As<Napi::Boolean>().Value(), true);

    (new uvWorkers::forSilentCache(info.Env(), cacheStruct, cacheStruct->waitingTasks))->Queue();
    return info.Env().Undefined();
  }
  
  // sourceHasAST option here doesn't exist, because function itself should check temlate's validity
  // addon.compile(templatePath: string, save: boolean, cb(errStatus?: Status), output?: string): void
  void compile(const Napi::CallbackInfo &info){
    processing::tasks::forAST* task;
    Napi::Env env = info.Env();
    {
      Napi::String jsTemplateName = info[0].As<Napi::String>();
      const std::string templateName = std::move(jsTemplateName.Utf8Value());
      Napi::Function cb = info[2].As<Napi::Function>();
      task = new processing::tasks::forAST(cb, info.Length() == 4 ? info[3].As<Napi::String>().Utf8Value() : std::string());
      processing::state& stateStruct = task->stateStruct;
      auto it = cache::dataMap.find(templateName);
      if(it!=cache::dataMap.end()) {
        Status status;
        it->second->mutex.lock();
        if( !(status = it->second->getStatus()) ) {
          it->second->waitingTasks->push_back(task);
          return it->second->mutex.unlock();
        }
        it->second->mutex.unlock();

        if(status < 0) {
          delete task;
          Napi::Array message = Napi::Array::New(env, 2u);
          message.Set(0u, jsTemplateName);
          message.Set(1u, Napi::Number::New(env, status));
          cb.Call({message});
          return;
        }
        else /*ok*/ {
          stateStruct.initForReadyCache(it->second->size);
        }
      }
      else {

        if(
            (stateStruct.descriptor = cross_os::OpenFileRead(templateName.data())) == cross_os::invalid_descriptor
            || (stateStruct.fileSize = cross_os::GetFileSize(stateStruct.descriptor)) == cross_os::invalid_file_size
        ){
          cross_os::CloseDescriptor(stateStruct.descriptor);
          delete task;
          cb.Call({
              Napi::Number::New(info.Env(), Status::CantUseFile)
          });
          return;
        }
        
        bool hasASTInSource = info[1].As<Napi::Boolean>().Value();

        // shouldSaveParam OR has appropriate size
        task->cacheStruct = new cache(
            templateName,
            hasASTInSource,
            (info[2].As<Napi::Boolean>().Value() || stateStruct.fileSize <= maxChunkSize)
        );
        task->cacheStruct->waitingTasks->push_back(task);
        (
          new uvWorkers::forSilentCache(info.Env(), task->cacheStruct, task->cacheStruct->waitingTasks)
        )->Queue();
      }
    }
    if(task->processAndCheckIfFinished(info.Env())) delete task;
  }
  void silentClearCache(const Napi::CallbackInfo &info){
    std::string str = std::move(info[0].As<Napi::String>().Utf8Value());
    auto it = cache::dataMap.find(str);
    if(it == cache::dataMap.end()) return;
    it->second->mutex.lock();
    if(!it->second->unbookAndCheckIfFree() || it->second->getStatus() == Status::PendingDiskRead)  return it->second->mutex.unlock();
    it->second->mutex.unlock();
    delete it->second;
    cache::dataMap.erase(str);
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

