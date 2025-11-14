#include "./include/shared2.hpp"
#include "./include/cross_os.hpp"
void TSFNFinalize(uv_work_t* req, int){
  //delete static_cast<caching::libuv::dataStruct*>(req->data);
}
inline void WorkerToCacheSendError(caching::Status status, caching::data::perhaps_streamed* cache){
  caching::dataMapType::accessor accessor;
  caching::dataMap.find(accessor, cache->filename);
  auto it = streaming::cacheDependentTasks.find(cache);
  if(it!=streaming::cacheDependentTasks.end()) {
    for(streaming::data::MinBase* task : it->second){
      task->tsfn.NonBlockingCall([status](Napi::Env env, Napi::Function jsCallback){
          jsCallback.Call({Napi::Number::New(env, status)});
          });
      // trigger tsfn Finalizer
      task->tsfn.Release();
    }
    streaming::cacheDependentTasks.unsafe_erase(cache);
  }
  delete cache;
  caching::dataMap.erase(accessor);
}
inline void WorkerToCacheBeforeProcessSendError(caching::Status status, caching::data::perhaps_streamed* cache){
  
}
void caching::cachingLibuvCB(uv_work_t* req){

  using Status = caching::Status;
  caching::data::perhaps_streamed* cache = static_cast<caching::data::perhaps_streamed*>(
      static_cast<caching::libuvDataStruct*>(req->data)->cacheOrTask
  );
  cross_os::descriptor_t descriptor = cross_os::OpenFileRead(cache->filename);
  if (descriptor == cross_os::invalid_descriptor) {
    return WorkerToCacheSendError(Status::NoFile, cache);
  }
  int64_t fileSize = cross_os::GetFileSize(descriptor);
  if (fileSize == cross_os::invalid_file_size) {
    cross_os::CloseDescriptor(descriptor);
    return WorkerToCacheSendError(Status::NoFile, cache);
  }
  cache->SetData(new char[fileSize], fileSize);
  if (!cache->pointer){
    cross_os::CloseDescriptor(descriptor);
    return WorkerToCacheSendError(Status::NoFile, cache);
  }
  bool successfullyRead =  cross_os::ReadFile(descriptor, cache->pointer, fileSize) == cross_os::ReadFailed;
  cross_os::CloseDescriptor(descriptor);
  if(!successfullyRead) return WorkerToCacheSendError(Status::NoFile, cache);
  {
    caching::dataMapType::accessor accessor;
    caching::dataMap.find(accessor, cache->filename);
    cache->status = Status::Ready;
    // unlock mutex here because cache was initialized. cacheDependentTasks add only if cache is not ready yet, so in fact since now no other thread will enqueue new ones
  }
  auto it = streaming::cacheDependentTasks.find(cache);
  if(it==streaming::cacheDependentTasks.end() || cache->busyLevel == 1) return;
  for(streaming::data::MinBase* task : it->second){
    streaming::workers.enqueue([task]{task->threadCB();});
  }
  streaming::cacheDependentTasks.unsafe_erase(cache);
}
void streaming::data::MinBase::cachingLibuvCB(uv_work_t* req){
  //FLOW: 1) get task, its state, its cache, its size, its descriptor 2) read file and if finished - close descriptor 3) 
  using Status = caching::Status;
  streaming::data::forOptimization* initiativeTask = static_cast<streaming::data::forOptimization*>(req->data);
  caching::data::perhaps_streamed* cache = initiativeTask->state.chunk;
  // VALID
  cross_os::descriptor_t descriptor = initiativeTask->state.input;
  uint32_t sizeToRead;
  {
    uint32_t sizeLeft = initiativeTask->state.inputSize - initiativeTask->state.processedInputSize;
    sizeToRead = sizeLeft > maxChunkSize ? maxChunkSize : sizeLeft;
  }
  if(
      cache->status==caching::Status::Pending 
      && !cache->SetData(new char[sizeToRead], sizeToRead)
  ){
    cross_os::CloseDescriptor(descriptor);
    initiativeTask->tsfn.NonBlockingCall([](Napi::Env env, Napi::Function jsCallback)->void{
        jsCallback.Call({Napi::Number::New(env, Status::CantAllocate)});
        });
    initiativeTask->tsfn.Release();
    return WorkerToCacheBeforeProcessSendError(Status::NoFile, cache);
  }
  uint32_t readAmount = cross_os::ReadFile(descriptor, cache->pointer, maxChunkSize);
  if(readAmount == cross_os::ReadFailed){
    //TODO
    cross_os::CloseDescriptor(descriptor);
    initiativeTask->tsfn.NonBlockingCall([](Napi::Env env, Napi::Function jsCallback)->void{
        jsCallback.Call({Napi::Number::New(env, caching::Status::CantGetSize)});
        });
    initiativeTask->tsfn.Release();
    return WorkerToCacheBeforeProcessSendError(Status::NoFile, cache);
  }
  initiativeTask->state.processedInputSize+=readAmount;
  bool hasFinished = initiativeTask->state.hasFinished();
  if(hasFinished)
    cross_os::CloseDescriptor(descriptor);
  if(cache->status==caching::Status::Pending){
    caching::dataMapType::accessor accessor;
    caching::dataMap.find(accessor, cache->filename);
    cache->status = Status::Ready;
  }
  streaming::workers.enqueue([initiativeTask]{initiativeTask->threadCB();});
}

