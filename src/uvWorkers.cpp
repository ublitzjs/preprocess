#include "./include/shared3.hpp"
#include <iostream>
static void uvSetStatus(caching::data* cache, Status status){
  cache->mutex.lock();
  cache->setStatus(status);
  cache->book();
  cache->mutex.unlock();
}
void uvWorkers::forSilentCache::Execute(){
  cross_os::descriptor_t descriptor = cross_os::OpenFileRead(cache->filename);
  if(descriptor == cross_os::invalid_descriptor){
    return uvSetStatus(cache, Status::CantRead);
  }
  int64_t fileSize = cross_os::GetFileSize(descriptor);
  if(fileSize == cross_os::invalid_file_size){
    cross_os::CloseDescriptor(descriptor);
    return uvSetStatus(cache, Status::CantGetSize);
  }
  if(cache->sourceFileHasAST){
    uint16_t astLength;
    if(cross_os::ReadFile(descriptor, &astLength, 2) == cross_os::invalid_file_size){
      cross_os::CloseDescriptor(descriptor);
      return uvSetStatus(cache, Status::CantRead);
    }
    fileSize -= astLength * sizeof(int);
    if(fileSize<=0) {
      cross_os::CloseDescriptor(descriptor);
      return uvSetStatus(cache, Status::AST_Failed);
    } 
    cache->AST.resize(astLength);   
    if(cross_os::ReadFile(descriptor, cache->AST.data(), astLength * sizeof(int)) == cross_os::invalid_file_size){
      cross_os::CloseDescriptor(descriptor);
      return uvSetStatus(cache, Status::CantRead);
    }
  } 
  char* fileData = new char[fileSize];
  if(!fileData){
    cross_os::CloseDescriptor(descriptor);
    return uvSetStatus(cache, Status::CantAllocate);
  }
  cache->size = fileSize;
  if(cross_os::ReadFile(descriptor, fileData, fileSize) == cross_os::invalid_file_size){
    delete[] fileData;
    cross_os::CloseDescriptor(descriptor);
    return uvSetStatus(cache, Status::CantRead);
  }
  cross_os::CloseDescriptor(descriptor);
  cache->mutex.lock();
  cache->pointer = fileData;
  cache->book();
  cache->setStatus(Status::JustInMemory);
  cache->mutex.unlock();
}
void uvWorkers::forStreaming::Execute(){
  cache->mutex.lock();
  bool firstEntry = !cache->getStatus();
  cache->mutex.unlock();

  uint32_t sizeToRead = firstEntry ? std::min<uint32_t>(maxChunkSize, state->fileSize) : cache->size - state->getSyntaxPartialsSize();
  state->fileOffset+=sizeToRead;

  bool sourceFileHasAST = false;
  char* fileData = cache->pointer;
  if(firstEntry){
    if(cache->sourceFileHasAST){
      sourceFileHasAST = true;
      uint16_t astLength;
      if(cross_os::ReadFile(state->descriptor, &astLength, 2) == cross_os::invalid_file_size){
        cross_os::CloseDescriptor(state->descriptor);
        return uvSetStatus(cache, Status::CantRead);
      }
      state->fileSize -= astLength * sizeof(int);
      if(state->fileSize<=0) {
        cross_os::CloseDescriptor(state->descriptor);
        return uvSetStatus(cache, Status::AST_Failed);
      }
      if(!cache->isGlobal && state->fileSize<=maxChunkSize){
        std::cout<<"template with AST inside "<<cache->filename<<" has problematic size. Either split it or make larger for it to be shared across multiple tasks.\n";
      }
      caching::fullData* optimizedCache = static_cast<caching::fullData*>(cache);
      optimizedCache->AST.resize(astLength);   
      if(cross_os::ReadFile(state->descriptor, optimizedCache->AST.data(), astLength * sizeof(int)) == cross_os::invalid_file_size){
        cross_os::CloseDescriptor(state->descriptor);
        return uvSetStatus(cache, Status::CantRead);
      }
    } 
    cache->size = cache->isGlobal ? state->fileSize : maxChunkSize;
    fileData = new char[cache->size];
    if(!fileData){
      cross_os::CloseDescriptor(state->descriptor);
      return uvSetStatus(cache, Status::CantAllocate);
    }
    state->writeablePtr = fileData;
    state->currentPtr = fileData;
  }
  if(cross_os::ReadFile(state->descriptor, state->currentPtr, sizeToRead) == cross_os::invalid_file_size){
    if(firstEntry) delete[] fileData;
    cross_os::CloseDescriptor(state->descriptor);
    return uvSetStatus(cache, Status::CantRead);
  }
  
  if(state->fileOffset == state->fileSize){
    cross_os::CloseDescriptor(state->descriptor);
    state->descriptor = cross_os::invalid_descriptor;
    if(firstEntry){
      cache->mutex.lock();
      cache->pointer = fileData;
      cache->setStatus(sourceFileHasAST ? Status::AST_Ready : Status::JustInMemory);
      cache->mutex.unlock();
    }
  }
  
}
void uvWorkers::Worker::OnOK(){
  if(cache->isGlobal){
    for(streaming::data::MinBase* task : *waitingTasks){
      //check each time, because every task can mark it invalid
      if(cache->getStatus()>0) task->mainProcessing(Env());
      else {
        task->emitError(Env());
        delete task;
      }
    }
  } 
  if(cache->getStatus() < 0){
    caching::dataMap.erase(cache->filename);
    cache->waitingTasks = nullptr;
    delete cache;
  }
}
