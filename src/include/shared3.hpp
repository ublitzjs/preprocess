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
using whatever_ptr_t = char*;
enum Status : int8_t { // < 0 - error, > 0 - good
  AST_Ready = 2,
  JustInMemory = 1,
  PendingDiskRead = 0,
  CantRead = -1,
  CantAllocate = -2,
  AST_Failed = -3
};
enum Action : uint8_t {
  JustRead = 0,
  Insert = 1,
  Remove = 2,
  WaitForInit = 3
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
  struct fullData;
  extern std::unordered_map<std::string, data*> dataMap;
}
namespace streaming {
  struct state {
    char* writeablePtr;
    char* currentPtr;
    uint64_t fileSize;
    uint64_t fileOffset;
    cross_os::descriptor_t descriptor = cross_os::invalid_descriptor;
    Action action;
    uint8_t ast_index = 0;
    // is used only when this is level_state
    uint16_t levelInstructionIndex = 0;
    inline uint8_t getSyntaxPartialsSize(){
      // when I copy syntax partials to chunk beginning, I set currentPtr to where partials end, while writeablePtr should be put to the beginning.
      return currentPtr - writeablePtr;
    }
    void initForReadyCache(uint32_t cacheSize){
      action = Action::JustRead;
      descriptor = cross_os::invalid_descriptor;
      fileSize = cacheSize;
      fileOffset = cacheSize;
    }
    state(cross_os::descriptor_t, caching::data*, bool cacheIsReady, uint64_t fileSize);
    state() = default;
  };
  // state for forFS or forJS
  struct level_state : public state {
    Napi::Reference<Napi::Value> levelInstructions;
    level_state(Napi::Value instructions) 
      : levelInstructions(Napi::Persistent(instructions)) {}
  };
  namespace data {
    struct MinBase {
      syntax* syntaxStruct;
      Napi::Reference<Napi::Function> jsCallback;
      virtual void emitError(Napi::Env);
      virtual void mainProcessing(Napi::Env);  
      virtual ~MinBase(){}
    protected:
      MinBase(Napi::Function fn) : jsCallback(Napi::Persistent(fn)) {}
    };
    struct forAST : public MinBase {
      cross_os::descriptor_t output;
      state stateStruct;
      caching::data* cache;
      void emitError(Napi::Env) override;
      void mainProcessing(Napi::Env) override;
      forAST(Napi::Function cb, caching::data* cache,cross_os::descriptor_t descriptor, bool cacheIsReady, uint64_t fileSize) 
        : MinBase(cb), cache(cache), stateStruct(descriptor, cache, cacheIsReady, fileSize) {}
       forAST(Napi::Function cb) : MinBase(cb) {}
    };
    struct Base : public MinBase {
      struct BookedCaches {
        void* data;
        BookedCaches(uint16_t amount) 
          :data(
              std::aligned_alloc(
                8, 
                // [ptr][ptr][ptr][signed 16bits][s16 bits][s16 bits]
                // ptr - cache, signed 16bits - usages. if -1 - no limit, hold to the function end
                amount * 10
              )
            ) {}
        inline caching::data*& getPtr(uint8_t index){
          return *(static_cast<caching::data**>(data) + index * 8);
        }
        inline int16_t& getUsages(uint8_t index, uint8_t amount){
          return *(static_cast<int16_t*>(data) +  2 * (4 * amount + index));
        }
        ~BookedCaches(){
          std::free(data);
        }
      };
      BookedCaches bookedCaches;
      Napi::Reference<Napi::Array> jsTemplatesList;
      Napi::Reference<Napi::Object> jsInstructions;
      std::stack<level_state> inclusions;
      void addNewLevel(cross_os::descriptor_t descriptor, caching::data* cache, bool cacheIsReady, uint64_t fileSize, Napi::Value instructions){
        inclusions.emplace(descriptor, cache, cacheIsReady, fileSize, instructions);
      }
    protected:
      Base(
          Napi::Function cb,
          Napi::Object jsInstructionsArg,
          Napi::Array jsTemplatesListArg,
          caching::data* cache
      ) : MinBase(cb),
          jsTemplatesList(Napi::Persistent(jsTemplatesListArg)),
          jsInstructions(Napi::Persistent(jsInstructionsArg)),
          bookedCaches(jsTemplatesListArg.Length())
      {
      }
      virtual ~Base(){}
    };
    class forFS : public Base {
      cross_os::descriptor_t output;
      std::vector<char*> chunks;
      void emitError(Napi::Env) override;
      void mainProcessing(Napi::Env) override;
    };
    class forJS : public Base {
      Napi::Reference<Napi::Array> chunks;
      void emitError(Napi::Env) override;
      void mainProcessing(Napi::Env) override;
    };
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
    };
    union {
      uint32_t size;
      // It is needed only when libuv caches for the first time.
      const bool sourceFileHasAST;
    };
    tbb::spin_mutex mutex;
    const bool isGlobal;
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
    data(const std::string& filenameReference, const bool hasASTInSourceFile, const bool isGlobal = false)
      : filename(
          static_cast<char*>(std::memcpy(
            new char[filenameReference.size() + 1],
            filenameReference.c_str(),
            filenameReference.size() + 1
            )
          )
        ),
        sourceFileHasAST(hasASTInSourceFile),
        isGlobal(isGlobal)
    {
      book();
    }
};
inline streaming::state::state(cross_os::descriptor_t descriptor, caching::data* cache, bool cacheIsReady, uint64_t fileSize) 
  : fileSize(fileSize), fileOffset(cacheIsReady?fileSize:0), descriptor(descriptor) {
    if(cacheIsReady) currentPtr = (writeablePtr = cache->pointer), action = Action::JustRead;
    else action = Action::WaitForInit;
  };
struct caching::fullData : caching::data {
  fullData(const std::string& filenameReference, const bool hasASTInSourceFile) : data(filenameReference, hasASTInSourceFile, true) {}
  void readTemplate();
  std::vector<int> AST;
};
extern uint32_t maxChunkSize;
namespace uvWorkers {
  class Worker : public Napi::AsyncWorker {
  public:
    explicit Worker(Napi::Env env) : Napi::AsyncWorker(env) {};
    caching::fullData* cache;
    union {
      streaming::data::MinBase* task;
      std::vector<streaming::data::MinBase*>* waitingTasks;
    };
    void OnOK() override;
  };
  class forSilentCache : public Worker {
  public:
    explicit forSilentCache(Napi::Env env) : Worker(env) {};
    void Execute() override {cache->readTemplate();};
  };
  class forStreaming : public Worker {
  public:
    explicit forStreaming(Napi::Env env) : Worker(env) {};
    void Execute() override;
    streaming::state* state;
  };
}
