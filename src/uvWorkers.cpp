#include "./include/shared3.hpp"
void uvWorkerErr(caching::data* cache, Status status){
  cache->mutex.lock();
  cache->setStatus(Status::CantRead);
  cache->mutex.unlock();
  for (streaming::data::MinBase* task : *cache->waitingTasks) {
    task->emitError();
    delete task;
  }
  delete cache->waitingTasks;
  cache->waitingTasks = nullptr;
}
void uvWorkers::forSilentCache::Execute(){
  cross_os::descriptor_t descriptor = cross_os::OpenFileRead(cache->filename);
  if(descriptor == cross_os::invalid_descriptor){
    return uvWorkerErr(cache, Status::CantRead);
  }
  int64_t fileSize = cross_os::GetFileSize(descriptor);
  if(fileSize == cross_os::invalid_file_size){
    cross_os::CloseDescriptor(descriptor);
    return uvWorkerErr(cache, Status::CantGetSize);
  }
  if(cache->sourceFileHasAST){
    uint16_t astLength;
    if(cross_os::ReadFile(descriptor, &astLength, 2) == cross_os::invalid_file_size){
      cross_os::CloseDescriptor(descriptor);
      return uvWorkerErr(cache, Status::CantRead);
    }
    fileSize -= astLength * sizeof(int);
    if(fileSize<=0) {
      cross_os::CloseDescriptor(descriptor);
      return uvWorkerErr(cache, Status::AST_Failed);
    }
    cache->AST.resize(astLength);   
    if(cross_os::ReadFile(descriptor, cache->AST.data(), astLength * sizeof(int)) == cross_os::invalid_file_size){
      cross_os::CloseDescriptor(descriptor);
      return uvWorkerErr(cache, Status::CantRead);
    }
  } 
  char* fileData = new char[fileSize];
  if(!fileData){
    cross_os::CloseDescriptor(descriptor);
    return uvWorkerErr(cache, Status::CantAllocate);
  }
  if(cross_os::ReadFile(descriptor, fileData, fileSize) == cross_os::invalid_file_size){
    delete[] fileData;
    cross_os::CloseDescriptor(descriptor);
    return uvWorkerErr(cache, Status::CantRead);
  }
  cross_os::CloseDescriptor(descriptor);
  std::vector<streaming::data::MinBase*>* waitingTasks = cache->waitingTasks;
  cache->mutex.lock();
  cache->pointer = fileData;
  cache->book();
  cache->setStatus(Status::PendingAST);
  cache->mutex.unlock();
}
void uvWorkers::forSilentCache::OnOK(){
  cache->mutex.lock();
  if(cache->unbookAndCheckIfFree() || (cache->getBusyLevel() && cache->getStatus() < 0)){
    caching::dataMap.erase(cache->filename);
    cache->mutex.unlock();
    delete cache;
    return;
  }
  cache->mutex.unlock();
}
