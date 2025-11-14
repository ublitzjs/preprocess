#pragma once
#include <regex>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <utility>
#include <uv.h>
#include <stack>
#include <string>
#include <vector>
#include <stdint.h>
#include <queue>
#include <napi.h>
#include <tbb/mutex.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_hash_map.h>
#include "./cross_os.hpp"
enum Action : uint8_t {
  JustRead = 0,
  Insert = 1,
  Remove = 2
};
class ThreadPools {
private:
  std::vector<std::thread> threads;
  std::queue<std::function<void()>> queue;
  std::mutex queueMutex;
  std::condition_variable synchronize;
  bool shouldStop = false;
public:
  ThreadPools() = default;
  void Init(uint8_t amount) {
    threads.reserve(amount);
    for(uint8_t i = 0; i < amount; i++){
      threads.emplace_back([this] () -> void {
        do {
          std::unique_lock<std::mutex> lock(queueMutex);
          synchronize.wait(lock, [this] {return shouldStop || !queue.empty();});
          if(queue.empty() && shouldStop) return;
          std::function<void()> task = std::move(queue.front());
          queue.pop();
          lock.unlock();
          task();
        } while (true);
      });
    }
  }
  // just to avoid constructing std::function<void()> each time
  template<typename Task>
  void enqueue(Task&& task){
    std::unique_lock<std::mutex> lock(queueMutex);
    queue.emplace(
      std::forward<Task>(task)
    );
    lock.unlock();
    synchronize.notify_one();
  }
  void Stop(){
    std::unique_lock<std::mutex> lock(queueMutex);
    shouldStop = true;
    lock.unlock();
    synchronize.notify_all();
    for(std::thread& thread : threads) thread.join();
  }
};
namespace syntax {
  struct dataStruct { // use std::string due to Small String Optimization
    const std::regex pattern;
    const std::string prefix;
    const std::string insertOn;
    const std::string prefixedInsertOn; // used in js worker 
    const std::string removeOn;
    const std::string end;
    // max among insertOn and removeOn
    const uint8_t maxParamLength;
    const uint8_t syntaxPartialsSize;
    dataStruct(
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
  };
  extern std::vector<syntax::dataStruct> dataVector;
  inline syntax::dataStruct* findMatchingStruct(const std::string& patternString){
    for(syntax::dataStruct& syntax : dataVector){
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
}
namespace caching {
  // >= 0 ? GOOD : BAD
  enum Status : int8_t {
    AST_Ready = 2,
    NoAST_Yet = 1,
    PendingDiskRead = 0,
    NoFile = -1,
    CantGetSize = -2,
    CantRead = -3,
    CantAllocate = -4,
    AST_Failed = -5
  };
  namespace data {
    // only status (and AST-related stuff in full cache) should be accessed in thread-safe manner by blocking dataMap. Everything else - only caching thread touches.
    struct perhaps_streamed {
      // if not cached yet - nullptr
      char* const pointer;
      const char* const filename;
      const uint32_t size;
      // whenever streaming worker gets some task, cache already is set to at least 1 and shouldn't be touched when workers picks it.
      uint16_t busyLevel = 1;
      Status status = Status::PendingDiskRead;
      bool canHaveAST = false; // if true - it is either "precompiled" and read (entirely or streamed) from file with syntax described in it already OR the file is cached entirely and will have all syntax saved as soon as it appears to get processed
      // it creates a dummy. caching::dataMap needs to have a sign that file is BEING cached right now.
      perhaps_streamed(): pointer(nullptr), filename(nullptr), size(0) {};
      perhaps_streamed(const std::string& filenameReference): 
        pointer(nullptr),
        filename(
            static_cast<const char*>(
              std::memcpy(
                new char[filenameReference.size() + 1],
                filenameReference.c_str(),
                filenameReference.size() + 1
                )
              )
            ),
        size(0)
      {}
      inline bool SetData(char* const chunkPointer, uint32_t mainChunkSizeParam){
        // I do these SCARY casts just because constructor creates a DUMMY. Init function actually makes dummy usable 
        *const_cast<char**>(&pointer) = chunkPointer;
        *const_cast<uint32_t*>(&size) = mainChunkSizeParam;
        return chunkPointer;
      }
      ~perhaps_streamed(){
        delete filename;
        delete pointer;
    }
    };
    #pragma pack(push, 1) // less memory, better to write to file.
    struct AST_Item {
      uint32_t length;
      // no REMOVE state because such templates don't need to be analyzed.
      bool isItForInterpolation;    
    };
    #pragma pack(pop)

    // when template is cached completely in map - it can be optimized in runtime or was read with optimization already in it. If it is streamed - it cannot be optimized in runtime but can be read from file with optimization in it.
    struct perhaps_optimized : public perhaps_streamed {
      std::vector<AST_Item> AST;
      std::atomic<uint16_t> AST_CurrentAmount = 0;
      perhaps_optimized(const std::string& templateName) : perhaps_streamed(templateName) {}
    };
  }
  using dataMapType = tbb::concurrent_hash_map<std::string, data::perhaps_streamed*>;
  extern dataMapType dataMap;
  // As it is created with unlimited queue - NonBlockingCall is not a worry
  extern Napi::ThreadSafeFunction emitter;
  // I use libuv ONLY for caching
  struct libuvDataStruct {
    uv_work_t uv_request; // when instance get deleted - request data as well;
    void* const cacheOrTask; // using it I will find out its size, if it should be streamed.
  };

  void cachingLibuvCB(uv_work_t* req);  // This function is queued when js calls .cache. 
  //inline void enqueue(uv_work_t* request){
  //  uv_queue_work(
  //      uv_default_loop(),
  //      request,
  //      ExecuteWork,
  //      TSFNFinalize
  //      );
  //}
  
  
  //void*  - 
  extern tbb::concurrent_unordered_map<caching::data::perhaps_streamed*, void*> temporaryCacheData;
}
namespace streaming {
  extern ThreadPools workers;
  namespace inclusion {
    struct stateStruct {
      Action action = Action::JustRead;
      uint8_t syntaxPartialsSize = 0;
      uint32_t processedInputSize = 0;
      uint32_t inputSize;
      //not necessarily valid.
      cross_os::descriptor_t input;
      caching::data::perhaps_streamed* chunk;
      char* chunk_dynamicPointer;
      stateStruct(caching::data::perhaps_streamed* chunk) : chunk(chunk) {
        if(!chunk->isNotStreamed()) {
          input = cross_os::OpenFileRead(chunk->filename);
          if(input == cross_os::invalid_descriptor) {
            //TODO
          }
          {
            int64_t inputSizeVar = cross_os::GetFileSize(input);
            if(inputSizeVar == cross_os::invalid_file_size){
              //TODO
            }
            inputSize = inputSizeVar;
          }

        } else {
          inputSize = chunk->size;
          input = cross_os::invalid_descriptor;
        }

      };
      /**
       * This is a pointer to where the stream can start consuming text.
       **/
      char* chunk_writablePointer = nullptr; 
      inline char* chunk_movePointerForward(uint16_t bytesDistance) noexcept;
      inline uint16_t chunk_pointerMovedDistance() const noexcept;
      inline char* chunk_findTemplateSyntax(const std::string& symbols) const noexcept;
      inline char* chunk_findTemplateSyntax(const std::string& symbols, uint8_t maxLength) const noexcept;
      inline void copySyntaxPartials(uint8_t offsetFromEnd) {
        std::memcpy(chunk->pointer, chunk->pointer + chunk->size - offsetFromEnd, offsetFromEnd);
      };
      inline bool hasFinished() const noexcept {
        return inputSize == processedInputSize;
      };
    };
    struct level {
      enum StatePreparationStatus : uint8_t {
        StillUsable = 0,
        LevelFinished = 1,
        AwaitingCache = 2
      };
      stateStruct currentState;
      level(Napi::Object param) : currentState(param) {
        
      }
      // returns true if next actually exists. If no -> move to the previous position in stack inside streaming::dataStruct
      inline virtual StatePreparationStatus prepareNext() {
        return StatePreparationStatus::StillUsable;
      };
    };
    struct level_multiple: public level {
      Napi::Array values;
      mutable uint8_t id = 0;
      inline StatePreparationStatus prepareNext() override {
        return StatePreparationStatus::StillUsable;
      };
    };
  }
  namespace data {
    class BookedCaches {
      static constexpr uint8_t cacheTotalAlignment = (
        sizeof(uint16_t) + sizeof(caching::data::perhaps_streamed*)
      );
      // it is like this ([num,cache][num,cache][num,cache]).
      // Just because structs forse alignment and I don't want 7-byte padding
      void* pointer;
    public:
      BookedCaches(uint16_t amount) 
        : pointer(
            std::memset(std::malloc(amount * cacheTotalAlignment), 0, amount * cacheTotalAlignment)
          ) {}
      ~BookedCaches(){
        std::free(pointer);
      }
      template<typename TYPE = uint16_t>
      inline TYPE getCacheRemainingUsages(uint16_t position){
        return *(static_cast<uint16_t*>(pointer) + position * cacheTotalAlignment);
      }
      inline caching::data::perhaps_streamed*& getCache(uint16_t position){
        return *(static_cast<caching::data::perhaps_streamed**>(pointer) + position * cacheTotalAlignment + 2);
      }
    };
    class MinBase {
    public:
      const syntax::dataStruct* const syntaxStruct;
      const Napi::ThreadSafeFunction tsfn;
      virtual void markConsumableChunk() = 0;
      virtual void sendChunks() = 0;
      virtual void threadCB() = 0;
      virtual bool shouldCacheWholeFile() = 0;
      virtual void sendError(caching::Status, caching::data::perhaps_streamed*) = 0;
      virtual inclusion::stateStruct& getCurrentState() = 0;
      static void cachingLibuvCB(uv_work_t* req);
    static void Finalizer(Napi::Env, MinBase* task, void*){
        delete task;
    }
    protected:
      virtual ~MinBase() = 0; // it won't manage tsfn.Release
      MinBase(
          Napi::Env env,
          Napi::Function& jsCallback,
          const syntax::dataStruct* const syntaxStruct,
          caching::data::perhaps_streamed* const initialTemplate
        ) :
        syntaxStruct(syntaxStruct),
        tsfn(Napi::ThreadSafeFunction::New(
          env,
          jsCallback,
          "Streaming js callback",
          0,
          1,
          this,
          Finalizer,
          (void*) nullptr
        ))
      {}
    };
    class Base : public MinBase {
    public:
      Napi::Reference<Napi::Object> params;
      Napi::Reference<Napi::Array> inputsList;
      BookedCaches bookedCaches;
      std::stack<inclusion::level> recursiveInclusions;
      inclusion::stateStruct& getCurrentState() override {
        return recursiveInclusions.top().currentState;
      }
      void cleanCaches(){
        caching::data::perhaps_streamed* currentCachePointer;
        for(uint8_t i = inputsList.Value().Length(); i>0; i--){
          // if cache has ever been actually used OR has not been cleared yet
          if(
              !(currentCachePointer = bookedCaches.getCache(i))
          ) continue;
          caching::dataMapType::accessor accessor;
          caching::dataMap.find(accessor, currentCachePointer->filename);
          // if cache is not booked 
          if(!currentCachePointer->busyLevel--){
            caching::dataMap.erase(accessor);
            delete currentCachePointer;
          }
        }
      }
    protected:
      virtual ~Base(){}
      static void Finalizer(Napi::Env, Base* task, void*){
        delete task;
      }
      Base(
          Napi::Object& paramsArg,
          Napi::Array& files,
          Napi::Env env,
          Napi::Function& jsCallback,
          const syntax::dataStruct* const syntaxStruct,
          caching::data::perhaps_streamed* const initialTemplate
        ) :
        MinBase(env, jsCallback, syntaxStruct, initialTemplate),
        bookedCaches(files.Length())
      {
        bookedCaches.getCache(0) = initialTemplate;
        bookedCaches.getCacheRemainingUsages<uint16_t&>(0) = 0;
        recursiveInclusions.push(params.Value());
      }
    };
    class forJS : public Base {
      Napi::Array chunks;
    public:
      forJS(
          Napi::Object& paramsArg,
          Napi::Array& files,
          Napi::Env env,
          Napi::Function& jsCallback,
          const syntax::dataStruct* const syntaxStruct,
          caching::data::perhaps_streamed* const initialTemplate
      ) : Base(paramsArg,files, env, jsCallback, syntaxStruct,initialTemplate)  {}
      ~forJS(){
        this->cleanCaches();
      }
      void threadCB() override;
      void markConsumableChunk() override;
      void sendChunks() override;
    };
    class forFS : public Base {
      std::vector<char*> chunks;
      cross_os::descriptor_t output;
    public:
      forFS(
          cross_os::descriptor_t output,
          Napi::Object& paramsArg,
          Napi::Array& files,
          Napi::Env env,
          Napi::Function jsCallback,
          const syntax::dataStruct* const syntaxStruct,
          caching::data::perhaps_streamed* const initialTemplate
      ) : Base(paramsArg,files, env, jsCallback, syntaxStruct,initialTemplate), output(output)  {}
      void threadCB() override;
      void markConsumableChunk() override;
      void sendChunks() override;
    };
    class forOptimization : public MinBase {
      public:
      streaming::inclusion::stateStruct state;
      const bool saveCacheInMap;
      forOptimization(
          bool saveCacheInMap,
          Napi::Env env,
          Napi::Function& jsCallback,
          const syntax::dataStruct* const syntaxStruct,
          caching::data::perhaps_streamed* const initialTemplate
        ) : MinBase(env, jsCallback, syntaxStruct, initialTemplate), saveCacheInMap(saveCacheInMap) , state(initialTemplate){}
      void threadCB() override;
      void libuvCB() {
        
      }
      void markConsumableChunk() override;
      void sendChunks() override;
      bool shouldCacheWholeFile() override {return saveCacheInMap;}
      inclusion::stateStruct& getCurrentState() override {
        return state;
      }

    };
  }
}
extern uint32_t maxChunkSize;
