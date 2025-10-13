#pragma once
#include <stdint.h>
#include <regex>
#include <napi.h>
#include <iostream>
#include <chrono>

template <typename T>
struct LightBuffer {
  T source;
  uint32_t length;
};

struct PatternStruct {
  std::regex pattern;
  std::string prefix;
  std::string insertOn;
  std::string removeOn;
  std::string end;
  /*This param is used as a size for additionalBuffer*/
  const uint8_t maxParamLength;
  const uint8_t maxSyntaxPartialsSize;
};
class Timer {
private:
	std::chrono::time_point<std::chrono::high_resolution_clock> start_Timepoint;
	const char* subject;
	bool logged = false;
public:
	Timer(const char* subject)
		: start_Timepoint(std::chrono::high_resolution_clock::now()), subject(subject)
	{}
	~Timer() {
		if (!logged) Stop();
	}
	void Stop() {
		logged = true;
		auto endTimePoint = std::chrono::high_resolution_clock::now();
		auto start = std::chrono::time_point_cast<std::chrono::microseconds>(start_Timepoint).time_since_epoch().count();
		auto end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimePoint).time_since_epoch().count();
		auto duration = (end - start) * 0.001;
		std::cout << subject << " duration: " << duration << "ms\n";
	}
};

extern uint32_t maxChunkSize;
constexpr uint8_t maxInsertKeyLength = 80;

class BaseStream {
protected:	
  int input;
public:
  const PatternStruct* const patternStruct;
protected:
  /*This is a pointer to position of chunk's first byte. It's set at class initialization and is not meant to change.*/
  Napi::ThreadSafeFunction tsfn;
  /**
   *  A pointer within a chunk, which is constantly moved to find template's syntax.
   * */
    BaseStream(const PatternStruct* patternStr, const std::string inputPath, Napi::ThreadSafeFunction tsfn);
  uint32_t inputSize, bytesRead = 0;
  const char*  chunk_initialPointer = nullptr;
public:
  char* chunk_currentPointer = nullptr;
  /*
   * This buffer is allocated ONCE, depending on syntax of template.
   * I use it ONLY when I can't find a prefix OR an action due to chunk ending and PROBABLY splitting syntax on 2 parts
   * When I can't find an action, I copy prefix AND POTENTIAL part of action to this buffer, and then on next chunk read copy it the beginning of the chunk, so that two POTENTIAL parts unite. If my assumptions failed, then prefix is FAKE And I mark it with a pointer. 
   * */
  char* syntaxPartials = nullptr;
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


  enum Action {
    None = 0 /*It means that template is just being read and streamed to destination (js/fs)*/,
    Inserting = 1 /*Chunk ends, prefix and action-symbols were found, but the ending symbols are not in this chunk and identifier to be "inserted" in template is split by a stream. Its part is saved to additionalBuffer and on chunk reread additionalBuffer is copied to the beginning of chunk to "combine" with a second part. */,
    Removing = 2 
  };
  Action action = Action::None;
  
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

void Worker(
    std::function<BaseStream*()> 
);

