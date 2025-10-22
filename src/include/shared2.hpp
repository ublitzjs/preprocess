#pragma once
#include <regex>
#include <functional>
#include <condition_variable>
#include <utility>
#include <uv.h>
//#include <stack>
#include <string>
#include <atomic>
#include <map>
#include <future>
#include <stdint.h>
#include <napi.h>
#include <queue>
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
  // I allocate such buffer |----|-| , where the end is syntaxPartials
  struct dataStruct {
    char* const pointer;
    /**
     * if nullprt, then there are 2 cases:
     * 1) if isReady == false, then it is just not initialised yet
     * 2) if isReady == true, then this file is actually too big to be read at once. 
     */
    const std::string* const filename;
    const uint32_t mainChunkSize;
    const uint8_t syntaxPartialsSize;
    // when someone called "cache" and set that tmp is "non-temp", it means that developer WILL clear it with clearCache MANUALLY and busyLevel+=1 (just not to disappear before time comes). 
    uint16_t busyLevel = 0;
    // dataStruct is created without any info when calling "cache". This flag changes when caching is complete. This ensures that calling "cache" when data is not initialized will fail. To wait for initialisation "std::atomic_ref<bool>::wait" is preferred.
    std::atomic<bool> isReady;
    bool tmp = true;
    // it creates a dummy. caching::dataMap needs to have a sign that file is BEING cached right now. When it finishes caching - waitUntilReady
    dataStruct(): pointer(nullptr), filename(nullptr), mainChunkSize(0), syntaxPartialsSize(0), isReady(false) {};
    inline bool isCachedInMap() const noexcept {
      return filename;
    }
    // after caching::map.emplace I get a string and set it
    void MarkAsCachedInMap(const std::string* const filenamePointer){
      *const_cast<const std::string**>(&filename) = filenamePointer;
    }
    void Init(bool tmpParam, char* const syntaxPartialsParam, uint8_t syntaxPartialsSizeParam, uint32_t mainChunkSizeParam) {
      // I do these SCARY casts just because constructor creates a DUMMY. Init function actually makes dummy usable 
      *const_cast<char**>(&pointer) = syntaxPartialsParam;
      *const_cast<uint8_t*>(&syntaxPartialsSize) = syntaxPartialsSizeParam;
      *const_cast<uint32_t*>(&mainChunkSize) = mainChunkSizeParam;
      if(!tmp) tmp = tmpParam; // Lifetime of a cache must NOT be descresed without "clearCache". Template is temporary by default. If someone initialized it (here - in a race condition) as a "true" value, then he/she must have a reason to extend its lifetime AND ia taking responsibility to clear it, when time comes.
      isReady.store(true, std::memory_order_relaxed);
      isReady.notify_all();
    }
    static inline void waitUntilReady(std::atomic<bool>* pendingReady){
      pendingReady->wait(false, std::memory_order_relaxed);
    }

    ~dataStruct(){
      delete pointer;
    }
  };
  extern std::map<std::string, dataStruct> dataMap;
  extern std::mutex dataMapMutex;
  extern Napi::ThreadSafeFunction emitter;
  extern ThreadPools workers;
}
  namespace libuvWorker {
    enum Statuses : uint8_t {
      Success = 0,
      NoFile = 1,
      CantGetSize = 2,
      CantRead = 3,
      CantAllocate = 4
    };
    struct dataStruct {
      uv_work_t uv_request; // when instance get deleted - request data as well;
      const syntax::dataStruct* const syntaxStruct;
      caching::dataStruct* const cache;
      const std::string templateName;
      std::promise<Statuses> sync;
      const bool cacheFullFile;
      const bool cacheIsTmp;
      dataStruct(
          bool cacheIsTmp,
          bool cacheFullFile,
          const syntax::dataStruct* const syntax,
          caching::dataStruct* cache,
          std::string templateName
      ) : syntaxStruct(syntax),
        cache(cache),
        templateName(std::move(templateName)),
        cacheFullFile(cacheFullFile),
        cacheIsTmp(cacheIsTmp) {}
    };
    
    void ExecuteWork(uv_work_t *req);
    void Finalize(uv_work_t *req, int status);
    inline void QueueWork(uv_work_t* request){
      uv_queue_work(
          uv_default_loop(),
          request,
          ExecuteWork,
          Finalize
      );
    }
    void JSCachingThreadPool(
          bool tmp,
          bool cacheFullFile,
          caching::dataStruct* cacheDummy,
          const syntax::dataStruct* syntaxStruct,
          std::string templateName
      );
  }
extern uint32_t maxChunkSize;

namespace streaming {
  extern ThreadPools workers;
}
//namespace processStackItems {
//  class Base {
//  protected:
//    // abstract class
//    Base() = default;
//  // these properties are connected to solely Single->Several polymorphism
//  public:
//    bool hasNextQueuedProcess = true;
//    /**
//     *  before getting next value ALWAYS check if it "hasNextQueuedProcess"
//     * */
//    virtual Napi::Object nextQueuedProcess();
//  public:
//
//  };
//  // Single recursive include of a template (js object) + template is already cached
//  class Single : public Base {
//  private:
//    Napi::Object data;
//  public:
//    Napi::Object nextQueuedProcess() override {
//      return (hasNextQueuedProcess=true, data);
//    }
//    Single(Napi::Object data) : data(data) {}
//  };
//  // same as above, but template is too big and is streamed
//  class SingleFS : public Single {
//    
//  };
//  // several templates are included at once (js array)
//  class Several : public Base {
//    Napi::Array data;
//  public:
//    Several(Napi::Array param) : data(param) {
//      if(!data.Length()){
//        hasNextQueuedProcess = false;
//      }
//    }
//    Napi::Object nextQueuedProcess() override {
//      static int16_t leftValues = data.Length();
//      if(--leftValues <= 0){
//        hasNextQueuedProcess = false;
//      }
//      return data.Get(data.Length() - leftValues).As<Napi::Object>();
//    };
//  };
//}
//namespace workers {
//  class Stream {
//    // first argument from js
//    Napi::ObjectReference params;
//    // second argument from js
//    Napi::ObjectReference files;
//    // callback from js
//    Napi::ThreadSafeFunction tsfn;
//
//    // on creation I loop over "files" and for each filename insert a corresponding pointer from caching::map. If there is no such -> nullptr. When I get to nullptr, I check its size
//    std::vector<caching::dataStruct*> chunks;
//
//    // This is meant for recursive includes
//    std::stack<processStackItems::Base> processes;
//
//    // All templates should follow same syntax
//    const syntax::dataStruct* patternStruct;
//  };
//  class BaseStream {
//    protected:	
//      int input;
//    public:
//      const syntax::dataStruct* const patternStruct;
//    protected:
//      /*This is a pointer to position of chunk's first byte. It's set at class initialization and is not meant to change.*/
//      Napi::ThreadSafeFunction tsfn;
//      BaseStream(const syntax::dataStruct* patternStr, const std::string inputPath, Napi::ThreadSafeFunction tsfn);
//    public:
//
//      virtual ~BaseStream();
//
//      inline virtual void write(const char* source, uint32_t size);
//      virtual void flushChunks();
//      inline virtual void insert(char* keyword, uint8_t size);
//
//      inline char* chunk_movePointerForward(uint16_t bytesDistance) noexcept;
//      inline uint16_t chunk_pointerMovedDistance() const noexcept;
//      inline char* chunk_findTemplateSyntax(const std::string& symbols) const noexcept;
//      inline char* chunk_findTemplateSyntax(const std::string& symbols, uint8_t maxLength) const noexcept;
//      inline void copySyntaxPartials(uint8_t offsetFromEnd);
//      inline bool hasFinished() const noexcept;
//      inline const char* chunk_getInitialPointer() const noexcept;
//      inline void readNewChunk(); 
//  };
//  enum Action {
//    None = 0 /*It means that template is just being read and streamed to destination (js/fs)*/,
//    Inserting = 1 /*Chunk ends, prefix and action-symbols were found, but the ending symbols are not in this chunk and identifier to be "inserted" in template is split by a stream. Its part is saved to additionalBuffer and on chunk reread additionalBuffer is copied to the beginning of chunk to "combine" with a second part. */,
//    Removing = 2  /*Don't push to stream chunks until the "end" is found*/
//  };
//
//  class Base {
//  public:
//    Napi::ThreadSafeFunction tsfn;
//    syntax::dataStruct* patternStruct;
//    uint32_t inputSize, bytesRead = 0;
//    // if it is - I can remove it directly with "delete". If it IS - try to remove it from cache::map
//    bool cacheIsLocal;
//    char *chunk_currentPointer;
//    /**
//     * This is a pointer to where the stream can start consuming text.
//     * */
//    char* chunk_writablePointer = nullptr; 
//    /*
//       *  This is not just a size of a chunk allocated, but an amount of bytes read the last time. If files is smaller than "maxChunkSize", then chunkSize is same as an allocated size.
//       * */
//    uint32_t chunk_size;
//    /*
//       *  It's a size of string, copied to syntaxPartials between chunk reads. When 0 - syntaxPartials is still allocated, but it is not in use.
//       * */
//    uint8_t syntaxPartialsSize;
//    Action action = Action::None;
//  };
//  class JS : public Base {
//    static void ThreadPool();
//  };
//}
