#include "./include/shared3.hpp"
#include <stringzilla/stringzilla.hpp>

// I don't look for "removeOn" syntax here
// When I "break" loop it means that I want to call finalizer. For this case it is libuv
bool processing::tasks::forAST::processAndCheckIfFinished(Napi::Env env){
  // here cache's chunk is ready to be processed AND its status > 0
  // BUT state can be WaitForInit, meaning that: 1) cache is JUST FROM LIBUV for the first time and all pointer are to be initialized
  if(stateStruct.action == Action::WaitForInit){
    stateStruct.writeablePtr = (stateStruct.currentPtr = cacheStruct->pointer);
    stateStruct.action = Action::JustRead;
  }
  char* endChunkPtr = cacheStruct->pointer + cacheStruct->size;
  do {
    if(stateStruct.action == Action::JustRead){
      const char* interpolation = sz_find(
          stateStruct.currentPtr,
          endChunkPtr - stateStruct.currentPtr,
          syntaxStruct->prefixedInsertOn.data(),
          syntaxStruct->prefixedInsertOn.length()
          );
      if(interpolation) {
        stateStruct.currentPtr += syntaxStruct->prefixedInsertOn.length();
        stateStruct.action = Action::Insert;
        if(interpolation == stateStruct.writeablePtr) stateStruct.writeablePtr = nullptr;
        else this->write(stateStruct.writeablePtr, interpolation - stateStruct.writeablePtr);
        uint32_t leftSize = endChunkPtr - stateStruct.currentPtr;
        if(!stateStruct.currentTemplateIsFinished()){
          if(leftSize < syntaxStruct->end.length() + 1 /*for interpolation key*/){
            stateStruct.setSyntaxPartialsSize(cacheStruct->pointer, leftSize);
            uvWorkers::forProcessing* worker = new uvWorkers::forProcessing(env, &stateStruct, cacheStruct);
            worker->task = this;
            worker->Queue();
            return false;

          } // else repeat cycle with insert action
        } else {
          if(leftSize < syntaxStruct->end.length() + 1){
            cacheStruct->setStatus(Status::AST_Failed);
            emitError(env);
            return true;
          } // else - repeat cycle with "insert" action
        }
      } else {
        // check if interpolation syntax PARTIAL (length - 1) could fit in this chunk
        uint32_t movedDistance = endChunkPtr - stateStruct.currentPtr;
        if(stateStruct.currentTemplateIsFinished()){
          this->write(stateStruct.writeablePtr, movedDistance);
          break;
        } else {
          uint8_t partialSyntaxSize = syntaxStruct->prefixedInsertOn.length() - 1;
          bool shouldStop;
          if(movedDistance > partialSyntaxSize)
            // auto handle libuv/cleanup
            this->write(stateStruct.writeablePtr, movedDistance - partialSyntaxSize);
          // syntax partials are needed ONLY in libuv, so that I read them in right place.
          // partial is a sign of cache being NON-GLOBAl. So only one task can wait for it -> libuv needs to edit state of only one task and not many.
          // I don't copy them here BECAUSE OF maybe ASYNC WRITE
          // After libuv used it - currentPtr = writeablePtr
          stateStruct.setSyntaxPartialsSize(cacheStruct->pointer, partialSyntaxSize);
          uvWorkers::forProcessing* worker = new uvWorkers::forProcessing(env, &stateStruct, cacheStruct);
          worker->task = this;
          worker->Queue();
          return false;
        }
      }

    } else /*here == Action::Insert*/ {

      // I get here ONLY when currentPtr points AFTER prefixedInsertOn exactly on interpolation key
      // Also distance is 
      char* end = const_cast<char*>(
        sz_find(
          stateStruct.currentPtr,
          syntaxStruct->maxInsertKeyLength + syntaxStruct->end.length(),
          syntaxStruct->end.data(),
          syntaxStruct->end.length()
        )
      );

      if(end){
        insert(stateStruct.currentPtr, end - stateStruct.currentPtr);
        stateStruct.currentPtr = end + syntaxStruct->end.length();
        uint32_t leftChunkSize = endChunkPtr - stateStruct.currentPtr;
        stateStruct.action = Action::JustRead;

        if(leftChunkSize < syntaxStruct->prefixedInsertOn.length()){

          // in .compile when processing ends - just quit. Task deletion will handle other stuff
          if(stateStruct.currentTemplateIsFinished() && leftChunkSize){
            write(stateStruct.currentPtr, leftChunkSize);
            break;
          } 
          stateStruct.setSyntaxPartialsSize(cacheStruct->pointer, leftChunkSize);
          uvWorkers::forProcessing* worker = new uvWorkers::forProcessing(env, &stateStruct, cacheStruct);
          worker->task = this;
          worker->Queue();
          return false;
        } // else repeat cycle with another action 
          
      } else {
        cacheStruct->setStatus(Status::AST_Failed);
        emitError(env);
        return true;
      }
    }
  } while(true);

  if(!output.empty()) {
    (new uvWorkers::compiledToOutput(env, this))->Queue();
    return false;
  }
  return true;
}
void processing::tasks::forAST::emitError(Napi::Env env){

}

bool processing::tasks::forFS::processAndCheckIfFinished(Napi::Env env){

}
void processing::tasks::forFS::emitError(Napi::Env env){

}

bool processing::tasks::forJS::processAndCheckIfFinished(Napi::Env env){

}
void processing::tasks::forJS::emitError(Napi::Env env){

}
