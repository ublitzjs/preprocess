#include <regex>
#include <string>
#include <stdint.h>
#include <napi.h>


namespace caches {
  // I allocate such buffer |_|____| , where the beginning is syntaxPartials
  struct dataStruct {
    char* const syntaxPartials;
    uint8_t syntaxPartialsSize;
    char* mainChunkBeginning;
    uint32_t mainChunkSize;
    // when someone called "cache" and set that tmp is "non-temp", it means that developer WILL clear it with clearCache MANUALLY and busyLevel+=1 (just not to disappear before time comes). 
    uint16_t busyLevel = 0;
    // dataStruct is created without any info when calling "cache". This flag changes when caching is complete. This ensures that calling "cache" when data is not initialized will fail. To wait for initialisation "std::atomic_ref<bool>::wait" is preferred.
    bool isReady = false;
    bool tmp = true;
    dataStruct(): syntaxPartials(nullptr){};
    void Init(bool tmpParam, char* const syntaxPartialsParam, uint8_t syntaxPartialsSizeParam, uint32_t mainChunkSizeParam) {
      *const_cast<char**>(&syntaxPartials) = syntaxPartialsParam;
      syntaxPartialsSize = syntaxPartialsSizeParam;
      mainChunkBeginning = syntaxPartials + syntaxPartialsSize;
      mainChunkSize = mainChunkSizeParam;
      if(!tmp) tmp = tmpParam; // Lifetime of a cache must NOT be descresed without "clearCache". Template is temporary by default. If someone initialized it (here - in a race condition) as a "true" value, then he/she must have a reason to extend its lifetime AND ia taking responsibility to clear it, when time comes.
      isReady = true;
    }

    ~dataStruct(){
      // deletes everything
      delete syntaxPartials;
    }
  };
  extern std::map<std::string, dataStruct> map;
  extern std::mutex mutex;
}
extern uint32_t maxChunkSize;

struct PatternStruct { // use std::string due to Small String Optimization
  std::regex pattern;
  std::string prefix;
  std::string insertOn;
  std::string prefixedInsertOn; // used in js worker 
  std::string removeOn;
  std::string end;
  // max among insertOn and removeOn
  const uint8_t maxParamLength;
  const uint8_t syntaxPartialsSize;
};

namespace workers {
  class BaseStream {
    protected:	
      int input;
    public:
      const PatternStruct* const patternStruct;
    protected:
      /*This is a pointer to position of chunk's first byte. It's set at class initialization and is not meant to change.*/
      Napi::ThreadSafeFunction tsfn;
      BaseStream(const PatternStruct* patternStr, const std::string inputPath, Napi::ThreadSafeFunction tsfn);
    public:

      virtual ~BaseStream();

      inline virtual void write(const char* source, uint32_t size);
      virtual void flushChunks();
      inline virtual void insert(char* keyword, uint8_t size);

      inline char* chunk_movePointerForward(uint16_t bytesDistance) noexcept;
      inline uint16_t chunk_pointerMovedDistance() const noexcept;
      inline char* chunk_findTemplateSyntax(const std::string& symbols) const noexcept;
      inline char* chunk_findTemplateSyntax(const std::string& symbols, uint8_t maxLength) const noexcept;
      inline void copySyntaxPartials(uint8_t offsetFromEnd);
      inline bool hasFinished() const noexcept;
      inline const char* chunk_getInitialPointer() const noexcept;
      inline void readNewChunk(); 
  };
  enum Action {
    None = 0 /*It means that template is just being read and streamed to destination (js/fs)*/,
    Inserting = 1 /*Chunk ends, prefix and action-symbols were found, but the ending symbols are not in this chunk and identifier to be "inserted" in template is split by a stream. Its part is saved to additionalBuffer and on chunk reread additionalBuffer is copied to the beginning of chunk to "combine" with a second part. */,
    Removing = 2  /*Don't push to stream chunks until the "end" is found*/
  };

  class Base {
  public:
    Napi::ThreadSafeFunction tsfn;
    PatternStruct* patternStruct;
    uint32_t inputSize, bytesRead = 0;
    // if it is - I can remove it directly with "delete". If it IS - try to remove it from cache::map
    bool cacheIsLocal;
    char *chunk_currentPointer;
    /**
     * This is a pointer to where the stream can start consuming text.
     * */
    char* chunk_writablePointer = nullptr; 
    /*
       *  This is not just a size of a chunk allocated, but an amount of bytes read the last time. If files is smaller than "maxChunkSize", then chunkSize is same as an allocated size.
       * */
    uint32_t chunk_size;
    /*
       *  It's a size of string, copied to syntaxPartials between chunk reads. When 0 - syntaxPartials is still allocated, but it is not in use.
       * */
    uint8_t syntaxPartialsSize;
    Action action = Action::None;
  };
  class JS : public Base {
    static void ThreadPool();
  };
  struct FSStackData {
    Napi::Object data;
    uint16_t nextIndexToVisit;
  };
 // class FS : public Base {
 //   public:
 //   Napi::ObjectReference dataRef;
 //   static void ThreadPool();
 //   std::vector<FSStackData> stack;
 //   FS(){
 //     stack.emplace_back(std::move(dataRef), 0); 
 //   }
 //   ~FS(){
 //     dataRef.Unref();
 //   }
 // };
}
