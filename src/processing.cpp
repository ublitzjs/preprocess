#include "./include/shared3.hpp"
#include <stringzilla/stringzilla.hpp>

// I don't look for "removeOn" syntax here
void processing::tasks::forAST::mainProcessing(Napi::Env env){
  // here cache's chunk is ready to be processed AND its status > 0
  // BUT state can be WaitForInit, meaning that: 1) cache is JUST FROM LIBUV for the first time and all pointer are to be initialized
  if(stateStruct.action == Action::WaitForInit){
    stateStruct.writeablePtr = (stateStruct.currentPtr = cacheStruct->pointer);
    stateStruct.action = Action::JustRead;
  }
  char* endChunkPtr = cacheStruct->pointer + cacheStruct->size;

  if(stateStruct.action == Action::JustRead){
    const char* interpolation = sz_find(
        stateStruct.writeablePtr,
        endChunkPtr - stateStruct.currentPtr,
        syntaxStruct->prefixedInsertOn.data(),
        syntaxStruct->prefixedInsertOn.length()
    );

    if(interpolation) {
           
    } else {
      // check if interpolation syntax PARTIAL (length - 1) could fit in this chunk
      uint32_t movedDistance = endChunkPtr - stateStruct.currentPtr;
      if(stateStruct.currentTemplateIsFinished()){
        write(stateStruct.writeablePtr, movedDistance);
      } else {
        uint8_t partialSyntaxSize = syntaxStruct->prefixedInsertOn.length() - 1;
        if(movedDistance > partialSyntaxSize)
          write(stateStruct.writeablePtr, movedDistance - partialSyntaxSize);
        std::memcpy(cacheStruct->pointer, endChunkPtr - partialSyntaxSize, partialSyntaxSize); 
        // syntax partials are needed ONLY in libuv, so that I read them in right place.
        // partial is a sign of cache being NON-GLOBAl. So only one task can wait for it -> libuv can edit state of only one task and not many.
        // After libuv used it - currentPtr = writeablePtr
        stateStruct.writeablePtr = cacheStruct->pointer;
        stateStruct.currentPtr = cacheStruct->pointer + partialSyntaxSize;
      }
    }

  }
}
void processing::tasks::forAST::emitError(Napi::Env env){

}

void processing::tasks::forFS::mainProcessing(Napi::Env env){

}
void processing::tasks::forFS::emitError(Napi::Env env){

}

void processing::tasks::forJS::mainProcessing(Napi::Env env){

}
void processing::tasks::forJS::emitError(Napi::Env env){

}
