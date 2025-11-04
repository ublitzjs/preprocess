#pragma once
#include <regex>
#include <functional>
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
    Pending = 0,
    Ready = 1,
    NoFile = -1,
    CantGetSize = -2,
    CantRead = -3,
    CantAllocate = -4,
    AST_Failed = -5
  };
  namespace data {
    struct perhaps_streamed {
      // if not cached yet - nullptr
      char* const pointer;
      /**
       * filename is a heap allocated string... Nullptr -> cache is streamed and not saved in dataMap
       */
      const char* const filename;
      const uint32_t size;
      // whenever streaming worker gets some task, cache already is set to at least 1 and shouldn't be touched when workers picks it.
      uint16_t busyLevel = 1;
      Status status = Status::Pending;
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
      inline bool isNotStreamed() const noexcept {
        return filename;
      }
      // after caching::map.emplace I get a string and set it
      inline void Init(char* const chunkPointer, uint32_t mainChunkSizeParam) {
        // I do these SCARY casts just because constructor creates a DUMMY. Init function actually makes dummy usable 
        *const_cast<char**>(&pointer) = chunkPointer;
        *const_cast<uint32_t*>(&size) = mainChunkSizeParam;
        status = Status::Ready;
      }
      ~perhaps_streamed(){
        delete filename;
        delete pointer;
    }
    };
    struct AST_Item {
      uint32_t offset;
      uint32_t length;
      // here "Remove" CAN'T appear, because removing option exists for something MUCH LESS DYNAMIC.
      Action purpose;    
    };
    // streamed templates can't use optimization
    struct full : public perhaps_streamed {
      std::vector<AST_Item>* AST = nullptr;
      tbb::mutex AST_Mutex;
      bool AST_Ready = false;
      bool shouldNotifyJSAboutAST;
      full(const std::string& templateName, bool waitForAST) : perhaps_streamed(templateName), shouldNotifyJSAboutAST(waitForAST) {}
    };
  }
  using dataMapType = tbb::concurrent_hash_map<std::string, data::perhaps_streamed*>;
  extern dataMapType dataMap;
  // As it is created with unlimited queue - NonBlockingCall is not a worry
  extern Napi::ThreadSafeFunction emitter;
  // I use libuv ONLY for caching
  namespace libuv {
    struct dataStruct; 
    void ExecuteWork(uv_work_t *req);
  void Finalize(uv_work_t *req, int status);
  inline void enqueue(uv_work_t* request){
    uv_queue_work(
        uv_default_loop(),
        request,
        ExecuteWork,
        Finalize
        );
  }
  }
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
          if(input == cross_os::invalid_descriptor_t) {
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
          input = cross_os::invalid_descriptor_t;
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
    static void Finalizer(Napi::Env, MinBase* task, void*){
        delete task;
    }
    protected:
      virtual ~MinBase(){}
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
      forJS(
          Napi::Object& paramsArg,
          Napi::Array& files,
          Napi::Env env,
          Napi::Function& jsCallback,
          const syntax::dataStruct* const syntaxStruct,
          caching::data::perhaps_streamed* const initialTemplate
      ) : Base(paramsArg,files, env, jsCallback, syntaxStruct,initialTemplate)  {}
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
      streaming::inclusion::stateStruct state;
      void threadCB() override;
      void markConsumableChunk() override;
      void sendChunks() override;
    };
  }
  // I use these tasks ONLY when corresponding cache in caching::dataMap is locked with accessor. That's why here I use more unsafe but fast unordered_map
  extern tbb::concurrent_unordered_map<caching::data::perhaps_streamed*, std::vector<data::Base*>> cacheDependentTasks;
  inline void enqueueCacheDependentTasks(caching::data::perhaps_streamed* cache){
    auto it = cacheDependentTasks.find(cache);
    if(it != cacheDependentTasks.end()){
     // for(dataStruct* task : it->second){
     //   workers.enqueue([]{
     //       // here call processing function
     //   });
     // }
    }
    cacheDependentTasks.unsafe_erase(cache);
  }
}
struct caching::libuv::dataStruct  {
      uv_work_t uv_request; // when instance get deleted - request data as well;
      void* const cacheOrStreamState; // using it I will find out its size, if it should be streamed. If should -> 
      const streaming::data::MinBase* const initiativeTask; // if no such - it is a .cache call from js.
      // when true - only FULL file is cached. Otherwise it depends on maxChunkSize
      inline void notifyCompletion(Status status);
      dataStruct(
          streaming::inclusion::stateStruct* state,
          const streaming::data::MinBase* const initiativeTask
      ) : cacheOrStreamState(state), initiativeTask(initiativeTask) {}
      dataStruct(
          caching::data::perhaps_streamed* cache,
          const streaming::data::MinBase* const initiativeTask
      ) : cacheOrStreamState(cache), initiativeTask(initiativeTask) {}
    };

inline void caching::libuv::dataStruct::notifyCompletion(caching::Status status){
  // here, compared to its derived class, there is no thread waiting for caching to end. That's why no offloading can be done.
  caching::dataMapType::accessor accessor;
  caching::dataMap.find(accessor, cache->filename);
  cache->status=status;
  if(shouldNotifyJS) notifyJS();
  if(cache->busyLevel) {
    if(status>0) return streaming::enqueueCacheDependentTasks(cache);
    auto it = streaming::cacheDependentTasks.find(cache);
    if(it == streaming::cacheDependentTasks.end()) return;
    for(streaming::data::Base* task : it->second){
      task->tsfn.NonBlockingCall([status](Napi::Env env, Napi::Function jsCallback){
        jsCallback.Call({Napi::Number::New(env, status)});
      });
    }
    streaming::cacheDependentTasks.unsafe_erase(cache);
  }
}

extern uint32_t maxChunkSize;
