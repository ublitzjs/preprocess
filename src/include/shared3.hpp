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
struct cache;
namespace processing {
  struct state {
    char* writeablePtr;
    char* currentPtr;
    int64_t fileSize;
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
    state(cross_os::descriptor_t, cache*, bool cacheIsReady, int64_t fileSize);
    state() : action(Action::WaitForInit) {};
    void initForReadyCache(uint32_t cacheSize){
      action = Action::JustRead;
      descriptor = cross_os::invalid_descriptor;
      fileSize = cacheSize;
      fileOffset = cacheSize;
    }
    bool currentTemplateIsFinished(){
      return fileSize<=fileOffset;
    }
  };
  // state for main_base
  struct level_state : public state {
    Napi::Reference<Napi::Value> levelInstructions;
    level_state(Napi::Value instructions)  
        : levelInstructions(Napi::Persistent(instructions)) {}
  };
  namespace tasks {
    struct abstract_base {
      syntax* syntaxStruct;
      Napi::Reference<Napi::Function> jsCallback;
      virtual void emitError(Napi::Env);
      virtual void mainProcessing(Napi::Env);  
      virtual ~abstract_base(){}
    protected:
      abstract_base(Napi::Function fn) : jsCallback(Napi::Persistent(fn)) {}
    };
    struct forAST : public abstract_base {
      cross_os::descriptor_t output;
      state stateStruct;
      cache* cacheStruct;
      void emitError(Napi::Env) override;
      void mainProcessing(Napi::Env) override;
      forAST(Napi::Function cb, cache* cache,cross_os::descriptor_t descriptor, bool cacheIsReady, uint64_t fileSize) 
        : abstract_base(cb), cacheStruct(cache), stateStruct(descriptor, cache, cacheIsReady, fileSize) {}
       forAST(Napi::Function cb) : abstract_base(cb) {}
    };
    struct main_base : public abstract_base {
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
        inline cache*& getPtr(uint8_t index) const {
          return *(static_cast<cache**>(data) + index * 8);
        }
        inline int16_t& getUsages(uint8_t index, uint8_t amount) const {
          return *(static_cast<int16_t*>(data) +  2 * (4 * amount + index));
        }
        ~BookedCaches(){
          std::free(data);
        }
      };
      BookedCaches bookedCaches;
      Napi::Reference<Napi::Array> jsTemplatesList;
      Napi::Reference<Napi::Object> jsInstructions;
      std::vector<level_state> inclusions;
    protected:
      main_base(
          Napi::Function cb,
          Napi::Object jsInstructionsArg,
          Napi::Array jsTemplatesListArg
      ) : abstract_base(cb),
          jsTemplatesList(Napi::Persistent(jsTemplatesListArg)),
          jsInstructions(Napi::Persistent(jsInstructionsArg)),
          bookedCaches(jsTemplatesListArg.Length())
      {}
      virtual ~main_base(){}
    };
    struct forFS : public main_base {
      cross_os::descriptor_t output;
      void emitError(Napi::Env) override;
      void mainProcessing(Napi::Env) override;
      forFS(
          Napi::Function cb,
          Napi::Object jsInstructionsArg,
          Napi::Array jsTemplatesListArg
      ) : main_base(cb, jsInstructionsArg, jsTemplatesListArg) {
      }
    };
    struct forJS : public main_base {
      Napi::Reference<Napi::Array> chunks;
      void emitError(Napi::Env) override;
      void mainProcessing(Napi::Env) override;
    };
  }
}
struct cache {
  static std::unordered_map<std::string, cache*> dataMap;
  public:
    char* filename;
    union {
      // data, not accessed from libuv - thread-safe
      char* pointer;
      // if cache status = 0 and is cached globally
      std::vector<processing::tasks::abstract_base*>* waitingTasks;
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
    cache(const std::string& filenameReference, const bool hasASTInSourceFile, const bool isGlobal)
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
      if(isGlobal) waitingTasks = new std::vector<processing::tasks::abstract_base*>();
    }
  std::vector<int> AST;
  Status /*== 2 - ok, < 0 - Status*/ readAST(int64_t& fileSize, cross_os::descriptor_t);
  Status readTemplate(
    int64_t& fileSize,
    char*& fileData,
    cross_os::descriptor_t descriptor,
    bool firstEntry, 
    uint8_t syntaxPartialsSize = 0
  );
};
inline processing::state::state(cross_os::descriptor_t descriptor, cache* cacheStruct, bool cacheIsReady, int64_t fileSize) 
  : fileSize(fileSize), fileOffset(cacheIsReady?fileSize:0), descriptor(descriptor) {
    if(cacheIsReady) currentPtr = (writeablePtr = cacheStruct->pointer), action = Action::JustRead;
    else action = Action::WaitForInit;
  };
extern uint32_t maxChunkSize;
namespace uvWorkers {
  class Worker : public Napi::AsyncWorker {
  public:
    explicit Worker(Napi::Env env) : Napi::AsyncWorker(env) {};
    cache* cacheStruct;
    union {
      processing::tasks::abstract_base* task;
      std::vector<processing::tasks::abstract_base*>* waitingTasks;
    };
    void OnOK() override;
  };
  class forSilentCache : public Worker {
  public:
    explicit forSilentCache(Napi::Env env) : Worker(env) {};
    void Execute() override;  };
  class forProcessing : public Worker {
  public:
    explicit forProcessing(Napi::Env env) : Worker(env) {};
    void Execute() override;
    processing::state* stateStruct;
  };
}
