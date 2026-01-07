#pragma once
#include <napi.h>
#include <cstring>
#include <string>
#include <stdint.h> 
#include <tbb/spin_mutex.h>
#include <uv.h>
#include "cross_os.hpp"
enum Status : int8_t { // < 0 - error, > 0 - good
  Ready = 1,
  PendingDiskRead = 0,
  CantUseFile = -1,
  CantAllocate = -2,
  AST_Failed = -3
};
enum Action : uint8_t {
  JustRead = 0,
  Insert = 1,
  Remove = 2,
};

// an array of these gets generated only once, so can be NOT std::vector
struct syntax {
  struct sizesStruct {
    const uint8_t prefixLength;
    const uint8_t insertOnLength;
    const uint8_t removeOnLength;
    const uint8_t endLength;
  };
  struct pointersStruct {
    const char* prefix;
    const char* insertOn;
    const char* removeOn;
    const char* end;
  };
  static constexpr uint8_t fieldsAmount = 4;
  static constexpr const char* const fields[fieldsAmount] = {"prefix", "insertOn", "removeOn", "end"};
  union {
    uint8_t sizesArray[fieldsAmount];
    sizesStruct sizes;
  };
  const uint8_t maxParamLength;
  const uint8_t maxInsertKeyLength;
  union {
    const char* pointersArray[fieldsAmount];
    pointersStruct pointers;
  };
};
struct cache {
  const bool fileIsMapped;
  union {
    uint64_t size;
    // It is needed only when libuv caches for the first time.
    const bool sourceFileHasAST;
  };
  char* filename;
  char* pointer = nullptr;
#ifdef WIN32
  HANDLE inputDescriptor;
  HANDLE mappingDescriptor;
#endif
  cache(const std::string& filenameReference, const bool sourceFileHasAST, const bool fileIsMapped)
    : filename(
        static_cast<char*>(std::memcpy(
            new char[filenameReference.size() + 1],
            filenameReference.c_str(),
            filenameReference.size() + 1
            )
          )
        ),
    sourceFileHasAST(sourceFileHasAST),
    fileIsMapped(fileIsMapped)
  {
    
  }
  ~cache(){
#if WIN32
    if(fileIsMapped) {
      UnmapViewOfFile(pointer);
      CloseHandle(mappingDescriptor);
      CloseHandle(inputDescriptor);
    } else {
      delete[] pointer;
    }
#else
    
#endif
  }
  std::vector<int> AST;
};
extern uint32_t maxChunkSize;
extern Napi::Reference<Napi::Function> cachingEmitter;
extern std::string AST_dir;
class uvWorker : public Napi::AsyncWorker {
  cache* cacheStruct;
  Napi::Reference<Napi::Int8Array> cacheInJS;
  Status cacheStatusToBeSetSooner = Status::Ready;
  
  explicit uvWorker(cache* cacheStruct, Napi::Function cb) : Napi::AsyncWorker(cb), cacheStruct(cacheStruct) {};
  inline void Execute() override {
    
    if(cacheStruct->fileIsMapped){
      
    }
  }
  inline void OnOK() override {
    cacheStruct->status = cacheStatusToBeSetSooner;
    cachingEmitter.Value().Call({cacheInJS.Value()});
  };
};
