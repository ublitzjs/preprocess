#pragma once
#include <napi.h>
#include <cstring>
#include <regex>
#include <string>
#include <stdint.h> 
#include <unordered_map>
#include <tbb/spin_mutex.h>
#include <uv.h>
#include "./cross_os.hpp"
// overall ~ 7 values, so 3 bits are enough to fit it inside
enum Status : int8_t { // < 0 - error, > 0 - good
  AST_Ready = 2,
  PendingAST = 1,
  PendingDiskRead = 0,
  CantGetSize = -1,
  CantRead = -2,
  CantAllocate = -3,
  AST_Failed = -4
};
enum Action : uint8_t {
  JustRead = 0,
  Insert = 1,
  Remove = 2
};
struct syntax {
  const std::regex pattern;
  const std::string prefix;
  const std::string insertOn;
  const std::string prefixedInsertOn; // used in js worker 
  const std::string removeOn;
  const std::string end;
  // max among insertOn and removeOn
  const uint8_t maxParamLength;
  const uint8_t syntaxPartialsSize;
  syntax(
      const std::string& patternString, 
      const std::string& prefix,
      const std::string& insertOn,
      const std::string& removeOn,
      const std::string& end,
      uint8_t maxInsertKeyLength
      ) : pattern(patternString),
  prefix(prefix),
  insertOn(insertOn),
  prefixedInsertOn(prefix + insertOn),
  removeOn(removeOn),
  end(end),
  maxParamLength(
      std::max<uint8_t>({
        static_cast<uint8_t>(insertOn.length()),
        static_cast<uint8_t>(removeOn.length())
        })
      ),
  syntaxPartialsSize(
      std::max<int16_t>({
        static_cast<uint8_t>(maxInsertKeyLength + end.size()) ,
        static_cast<uint8_t>(prefix.size() + maxParamLength),
        }) - 1 /*because PARTIALS*/
      ) {}
  static std::vector<syntax> dataVector;
  inline static syntax* findMatchingStruct(const std::string& patternString){
    for(syntax& syntax : dataVector){
      std::cmatch matches;
      if(
          std::regex_search(
            patternString.data(),
            matches,
            syntax.pattern
            )
        ) return &syntax;
    }
    return nullptr;
  }
};
namespace caching {
  struct data;
  extern std::unordered_map<std::string, data*> dataMap;
}
namespace streaming {
  struct state {
    Napi::Reference<Napi::Value> levelInstructions;
    union {
      caching::data* cache;
      caching::data** cacheInTemplatesList;
    };
    char* writeablePtr;
    char* currentPtr;
  private:
    // 7byte fileSize, 7byte processedFileSize, 2byte levelInstructionIndex. Also it is positioned so that levelInstructionIndex was 2byte aligned
    uint8_t packedInts[16];
  public:
    cross_os::descriptor_t descriptor;
    Action action = Action::JustRead;
    uint8_t ast_index;
    uint8_t syntaxPartialsSize = 0;
    uint16_t getLevelInstructionIndex(){
      return packedInts[14];
    }
    void setLevelInstructionIndex(uint16_t index){
      packedInts[14] = index;
    }
    uint64_t getFileSize(){
      uint8_t unwantedByte = packedInts[7];
      packedInts[7] = 0;
      uint64_t fileSize = *reinterpret_cast<uint64_t*>(packedInts);
      packedInts[7] = unwantedByte;
      return fileSize;
    }
    void setFileSize(uint64_t fileSize){
      uint8_t unwantedByte = packedInts[7];
      *reinterpret_cast<uint64_t*>(packedInts) = fileSize;
      packedInts[7] = unwantedByte;
    }
    uint64_t getProcessedFileSize(){
      uint8_t unwantedByte = packedInts[14];
      packedInts[14] = 0;
      uint64_t fileSize = *reinterpret_cast<uint64_t*>(packedInts);
      packedInts[14] = unwantedByte;
      return fileSize;
    }
    void incrementProcessedFileSize(uint32_t size){
      uint8_t unwantedByte = packedInts[14];
      packedInts[14] = 0;
      *reinterpret_cast<uint64_t*>(packedInts+7) += size;
      packedInts[14] = unwantedByte;
    }
  };
  namespace data {
    class MinBase {};
    class forAST : public MinBase {};
    class Base : public MinBase {};
    class forFS : public Base {};
    class forJS : public Base {};
  }
}
struct caching::data {
  public:
    char* filename;
    union {
      // data, not accessed from libuv - thread-safe
      char* pointer;
      // if cache status = 0 and is cached globally
      std::vector<streaming::data::MinBase*>* waitingTasks;
      // if cache  is streamed
      streaming::data::MinBase* singleWaitingTask;
    };
    union {
      uint32_t size;
      // It is needed only when libuv caches for the first time.
      bool sourceFileHasAST;
    };
    uint8_t AST_amount;
    tbb::spin_mutex mutex;
  protected:
    int16_t m_packedStatusAndBusyLevel = 0;
  public:
    inline Status getStatus() const {
      int8_t value = m_packedStatusAndBusyLevel & 0b111;
      if(value & 0b100)
        value |= ~0b111;
      return static_cast<Status>(value);
    }
    inline void setStatus(Status value){
      m_packedStatusAndBusyLevel &= ~0b111;
      m_packedStatusAndBusyLevel |= static_cast<int8_t>(value);
    };
    inline uint16_t getBusyLevel() const {
      return m_packedStatusAndBusyLevel >> 3;
    };
    inline void book(){
      uint16_t value = (m_packedStatusAndBusyLevel >> 3);
      value++;
      m_packedStatusAndBusyLevel &= ~(0x1FFF << 3);
      m_packedStatusAndBusyLevel |= (value << 3);
    }
    inline bool unbookAndCheckIfFree() {
      uint16_t value = (m_packedStatusAndBusyLevel >> 3);
      if(value) value--; 
      m_packedStatusAndBusyLevel &= ~(0x1FFF << 3);
      m_packedStatusAndBusyLevel |= (value << 3);
      return !value;
    }
    data(std::string& filenameReference, bool shouldBeStreamed, bool hasASTInSourceFile)
      : filename(
          static_cast<char*>(std::memcpy(
            new char[filenameReference.size() + 1],
            filenameReference.c_str(),
            filenameReference.size() + 1
            )
          )
        )
    {
      book();
    }
};
extern uint32_t maxChunkSize;
 
namespace uvWorkers {
  class forSilentCache : public Napi::AsyncWorker {
  public:
    explicit forSilentCache(Napi::Env env) : Napi::AsyncWorker(env) {};
    void Execute() override;
    void OnOK() override;
    caching::data* cache;
  };
  class forStreaming : public Napi::AsyncWorker {
  public:
    explicit forStreaming(Napi::Env env) : Napi::AsyncWorker(env) {};
    void Execute() override;
    void OnOK() override;
    streaming::data::MinBase* task;
  };
}



