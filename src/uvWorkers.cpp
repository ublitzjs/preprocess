#include "./include/shared3.hpp"
Status cache::readAST(int64_t& fileSize, cross_os::descriptor_t descriptor){
    uint16_t astLength;
    if(cross_os::ReadFile(descriptor, &astLength, 2) == cross_os::invalid_file_size){
      cross_os::CloseDescriptor(descriptor);
      return Status::CantUseFile;
    }
    fileSize -= astLength * sizeof(int);
    if(fileSize<=0) {
      cross_os::CloseDescriptor(descriptor);
      return Status::AST_Failed;
    } 
    AST.resize(astLength);
    if(cross_os::ReadFile(descriptor, AST.data(), astLength * sizeof(int)) == cross_os::invalid_file_size){
      cross_os::CloseDescriptor(descriptor);
      return Status::CantUseFile;
    }
    return Status::AST_Failed;
}
void uvWorkers::forSilentCache::Execute(){
  int64_t fileSize;
  char* fileData;
  cross_os::descriptor_t descriptor;
  if(
      (descriptor = cross_os::OpenFileRead(cacheStruct->filename)) == cross_os::invalid_descriptor ||
      (fileSize = cross_os::GetFileSize(descriptor)) == cross_os::invalid_file_size
  ){
    cacheStruct->mutex.lock();
    cacheStruct->setStatus(Status::CantUseFile);
    cacheStruct->book();
    cacheStruct->mutex.unlock();
    return;
  }
  Status result = cacheStruct->readTemplate(
      fileSize,
      fileData,
      descriptor,
      true
  );
  if(!result) delete[] fileData;
  cross_os::CloseDescriptor(descriptor);
  cacheStruct->mutex.lock();
  cacheStruct->setStatus(result);
  cacheStruct->pointer = fileData;
  cacheStruct->mutex.unlock();
};

Status cache::readTemplate(
    int64_t& fileSize,
    char*& fileData,
    cross_os::descriptor_t descriptor,
    uint8_t syntaxPartialsSize 
){
  Status result = Status::JustInMemory;
  uint32_t sizeToRead; 
  if(fileData && sourceFileHasAST){
    result = readAST(fileSize, descriptor);
    if(result<0) return result;
    size = (sizeToRead = std::min<uint32_t>(maxChunkSize, fileSize));
    fileData = new char[sizeToRead];
    if(!fileData){return Status::CantAllocate;}
  } else {
    fileData = pointer;
    sizeToRead =  size - syntaxPartialsSize;
  }
  return (cross_os::ReadFile(descriptor, fileData, sizeToRead) == cross_os::invalid_file_size) ? Status::CantUseFile : result;
}
void uvWorkers::forProcessing::Execute(){
  bool firstEntry = !cacheStruct->getStatus();
  uint8_t syntaxPartials = stateStruct->getSyntaxPartialsSize();
  char* fileData = firstEntry ? nullptr : cacheStruct->pointer + (stateStruct->action == Action::WaitForInit ? 0 : syntaxPartials);

  Status result = cacheStruct->readTemplate(
      stateStruct->fileSize,
      fileData,
      stateStruct->descriptor,
      syntaxPartials
  );

  if(fileData) stateStruct->fileOffset += cacheStruct->size - (fileData - cacheStruct->pointer);

  cacheStruct->mutex.lock();
  cacheStruct->setStatus(result);
  if(firstEntry) cacheStruct->pointer = fileData;
  cacheStruct->mutex.unlock();

  if(stateStruct->currentTemplateIsFinished() || result < 0){
    cross_os::CloseDescriptor(stateStruct->descriptor);
    stateStruct->descriptor = cross_os::invalid_descriptor;
  }
}

void uvWorkers::Worker::OnOK(){
  if(cacheStruct->isGlobal){
    for(processing::tasks::abstract_base* task : *waitingTasks){
      //check each time, because each task can mark it invalid
      if(cacheStruct->getStatus()>0) task->mainProcessing(Env());
      else {
        task->emitError(Env());
        delete task;
      }
    }
  } else {
    // automatically this is forProcessing worker, because local caches are only there.
    // + local caches means that there is single task waiting
    processing::tasks::abstract_base* task = static_cast<uvWorkers::forProcessing*>(this)->task;
    if(cacheStruct->getStatus()>0) task->mainProcessing(Env());
    else {
      task->emitError(Env());
      delete task;
    }
  }
  bool shouldBeFree = cacheStruct->unbookAndCheckIfFree();
  if(cacheStruct->getStatus() < 0 || shouldBeFree){
    cache::dataMap.erase(cacheStruct->filename);
    cacheStruct->waitingTasks = nullptr;
    delete cacheStruct;
  }
}
