#include <cstring>
#include <stdint.h>
#include "./include/shared.hpp"
#include <stringzilla/stringzilla.hpp>
#include <uv.h>
#include "./include/os.hpp"
#include <algorithm>
#include <future>
  
#define copy memcpy
//class Streams {
//private:
//	unsigned long long inputFileSize = 0, bytes_read = 0;
//public:
//	bool hasFinished() {
//		return inputFileSize == bytes_read;
//	}
//#if PLATFORM_APPROACH == 0
//private:
//	HANDLE input;
//	HANDLE output;
//public:
//	Streams(const ProcessRequirements* reqs, ChunkHelper& chunk) {
//		input = CreateFile(
//			reqs->inputFilename,
//			GENERIC_READ,
//			FILE_SHARE_READ,
//			NULL,
//			OPEN_EXISTING,
//			FILE_ATTRIBUTE_NORMAL,
//			NULL
//		);
//		if (input == INVALID_HANDLE_VALUE) {
//			std::cerr << "Couldn't open file " << reqs->inputFilename;
//			exit(1);
//		}
//		inputFileSize = GetFileSize(input, NULL);
//
//		output = CreateFile(
//			reqs->outputFilename,
//			GENERIC_WRITE,
//			FILE_SHARE_WRITE,
//			NULL,
//			CREATE_ALWAYS,
//			FILE_ATTRIBUTE_NORMAL,
//			NULL
//		);
//		if (output == INVALID_HANDLE_VALUE) {
//			std::cerr << "Couldn't create output file " << reqs->inputFilename;
//			CloseHandle(input);
//			exit(1);
//		}
//		chunk.additionalBuffer = new char[
//			reqs->startPreprocessor->sizeWithout1byte > reqs->endPreprocessor->sizeWithout1byte
//				? reqs->startPreprocessor->sizeWithout1byte
//				: reqs->endPreprocessor->sizeWithout1byte
//		];
//	}
//	~Streams() {
//		CloseHandle(input), CloseHandle(output);
//		globals::removingCode = false;
//	}
//	void write(const char* source, uint32_t size) {
//		if (WriteFile(output, source, size, nullptr, NULL) == FALSE) {
//			printf("Error: unable to write to file.\n GetLastError=%08x\n", GetLastError());
//			CloseHandle(input);
//			CloseHandle(output);
//			exit(1);
//		}
//	}	
//	inline void readNewChunk(ChunkHelper& chunk) {
//		unsigned long long have_left = inputFileSize - bytes_read;
//
//		chunk.pointer = globals::initialChunkPointer;
//		chunk.size = chunk.additionalBufferSize + have_left >= maxChunk
//			? ((bytes_read += maxChunk - chunk.additionalBufferSize), maxChunk)
//			: ((bytes_read += have_left), chunk.additionalBufferSize + have_left);
//
//		if (ReadFile(
//			input,
//			chunk.pointer + chunk.additionalBufferSize,
//			chunk.size - chunk.additionalBufferSize,
//			nullptr,
//			NULL
//		) == FALSE) {
//			printf("Error: Unable to read from file.\n GetLastError=%08x\n", GetLastError());
//			CloseHandle(input);
//			CloseHandle(output);
//			exit(1);
//		}
//
//		if (chunk.additionalBufferSize)
//			copy(chunk.pointer, chunk.additionalBuffer, chunk.additionalBufferSize);
//
//		chunk.additionalBufferSize = 0;
//	}
//#elif PLATFORM_APPROACH == 1
//private:
//  int input;
//  int output;
//public:
//  Streams(const ProcessRequirements* reqs, ChunkHelper& chunk){
//    input = open(reqs->inputFilename, O_RDONLY);
//    if(input == -1){
//			std::cerr << "Couldn't open file " << reqs->inputFilename;
//      exit(1);
//    }
//    output = open(reqs->outputFilename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
//    if(output == -1){
//			std::cerr << "Couldn't create output file " << reqs->outputFilename;
//      close(input);
//      exit(1);
//    }
//    { // get file's size
//      struct stat inputStats;
//      if(!fstat(input, &inputStats))
//        inputFileSize = inputStats.st_size;
//      else {
//        std::cerr<<"Couldn't get size of \"" << reqs->inputFilename << "\"";
//        exit(1);
//      }; 
//    }
//		chunk.additionalBuffer = new char[
//			reqs->startPreprocessor->sizeWithout1byte > reqs->endPreprocessor->sizeWithout1byte
//				? reqs->startPreprocessor->sizeWithout1byte
//				: reqs->endPreprocessor->sizeWithout1byte
//		];
//  }
//  ~Streams(){
//    close(input), close(output);
//    globals::removingCode = false;
//  }
//  void write(const char* source, uint32_t size){
//    if(::write(output, source, size) == -1) {
//			printf("Error: unable to write to file.");
//			close(input),close(output);
//			exit(1);
//    };
//  }
//	inline void readNewChunk(ChunkHelper& chunk) {
//		unsigned long long have_left = inputFileSize - bytes_read;
//
//		chunk.pointer = globals::initialChunkPointer;
//		chunk.size = chunk.additionalBufferSize + have_left >= maxChunk
//			? ((bytes_read += maxChunk - chunk.additionalBufferSize), maxChunk)
//			: ((bytes_read += have_left), chunk.additionalBufferSize + have_left);
//    if(
//      read(
//        input, 
//        chunk.pointer + chunk.additionalBufferSize,
//        chunk.size - chunk.additionalBufferSize
//      ) == -1
//    ){
//			printf("Error: Unable to read from file.\n");
//			close(input), close(output);
//			exit(1);
//    }
//
//		if (chunk.additionalBufferSize)
//			copy(chunk.pointer, chunk.additionalBuffer, chunk.additionalBufferSize);
//
//		chunk.additionalBufferSize = 0;
//  }
//#else
//private:
//	std::ifstream input;
//	std::ofstream output;
//public:
//	Streams(const ProcessRequirements * reqs, ChunkHelper & chunk) {
//		input.open(reqs->inputFilename, std::ios::in | std::ios::binary);
//		if (input.fail()) {
//			std::cerr << "Couldn't open file " << reqs->inputFilename;
//			exit(1);
//		}
//
//		// get file size
//		input.seekg(0, std::ios::end);
//		inputFileSize = input.tellg();
//		input.seekg(0, std::ios::beg);
//
//		output.open(reqs->outputFilename, std::ios::out | std::ios::binary);
//		if (output.fail()) {
//			input.close();
//			std::cerr << "Couldn't start writing to file " << reqs->outputFilename;
//			exit(1);
//		}
//
//		if (inputFileSize == 0) {
//			input.close(), output.close();
//			std::cerr << "Input file is empty";
//			exit(0);
//		}
//		chunk.additionalBuffer = new char[
//			reqs->startPreprocessor->sizeWithout1byte > reqs->endPreprocessor->sizeWithout1byte
//				? reqs->startPreprocessor->sizeWithout1byte
//				: reqs->endPreprocessor->sizeWithout1byte
//		];
//	}
//	~Streams() {
//		input.close();
//		output.close();
//		globals::removingCode = false;
//	}
//	void write(const char* destination, uint32_t size) {
//		output.write(destination, size);
//	}
//	inline void readNewChunk(ChunkHelper & chunk) {
//		unsigned long long have_left = inputFileSize - bytes_read;
//
//		chunk.pointer = globals::initialChunkPointer;
//		chunk.size = chunk.additionalBufferSize + have_left >= maxChunk
//			? ((bytes_read += maxChunk - chunk.additionalBufferSize), maxChunk)
//			: ((bytes_read += have_left), chunk.additionalBufferSize + have_left);
//
//		input.read(
//			chunk.pointer + chunk.additionalBufferSize,
//			chunk.size - chunk.additionalBufferSize
//		);
//
//		if (chunk.additionalBufferSize)
//			copy(chunk.pointer, chunk.additionalBuffer, chunk.additionalBufferSize);
//
//		chunk.additionalBufferSize = 0;
//	}
//#endif
//};
//
bool BaseStream::hasFinished()const noexcept{
  return inputSize == bytesRead;
}
BaseStream::BaseStream(const PatternStruct *patternStr, const std::string inputPath, Napi::ThreadSafeFunction tsfn)
    : input(open(inputPath.c_str(), O_RDONLY)), patternStruct(patternStr), tsfn(tsfn)
{
  if (input == -1) {
    std::cerr << "Couldn't open file " << inputPath;
    exit(1);
  }
  { // get file's size
    struct stat inputStats;
    if(!fstat(input, &inputStats))
      inputSize = inputStats.st_size;
    else {
      std::cerr<<"Couldn't get size of \"" << inputPath << '\"' << std::endl;
      exit(1);
    }; 
  }
  if(hasFinished()) return;
  chunk_initialPointer = new char[std::min<uint64_t>({maxChunkSize, inputSize})];
  chunk_writablePointer = (char*) chunk_initialPointer;
  syntaxPartials = new char[patternStruct->maxSyntaxPartialsSize];
}
  inline void BaseStream::write(const char* source, uint32_t size) {}
  void BaseStream::flushChunks(){}
  inline void BaseStream::insert(char* keyword, uint8_t size){}
BaseStream::~BaseStream() {
  close(input);
  delete[] chunk_initialPointer;
  delete[] syntaxPartials;
  tsfn.BlockingCall(
    static_cast<void*>(nullptr),
    [](Napi::Env env, Napi::Function jsCallback, const void *) {
      jsCallback.Call({env.Null(), env.Undefined()});
    }
  );
  tsfn.Release();
}
char *BaseStream::chunk_movePointerForward(uint16_t bytesDistance) noexcept {
  return chunk_currentPointer += bytesDistance;
}
uint16_t BaseStream::chunk_pointerMovedDistance() const noexcept {
  return chunk_currentPointer - chunk_initialPointer;
}
inline char* BaseStream::chunk_findTemplateSyntax(const std::string &symbols) const noexcept {
  const char *syntaxPrefix =
      sz_find(chunk_currentPointer, chunk_size - chunk_pointerMovedDistance(), symbols.c_str(),
              symbols.length());
  return (char*)syntaxPrefix;
}
inline char* BaseStream::chunk_findTemplateSyntax(const std::string &symbols, uint8_t maxLength) const noexcept {
  const char *syntaxPrefix =
      sz_find(chunk_currentPointer, maxLength, symbols.c_str(),
              symbols.length());
  return (char*)syntaxPrefix;
}
void BaseStream::copySyntaxPartials(uint8_t offsetFromEnd) {
  copy(syntaxPartials, chunk_initialPointer + chunk_size - offsetFromEnd, offsetFromEnd);
  syntaxPartialsSize = offsetFromEnd;
}
void BaseStream::readNewChunk() {
  unsigned long long haveLeft = inputSize - bytesRead;
  chunk_currentPointer = (char *)chunk_initialPointer;
  //can be zero
  chunk_size =
    syntaxPartialsSize + haveLeft >= maxChunkSize
    ? ((bytesRead += maxChunkSize - syntaxPartialsSize), maxChunkSize)
    : ((bytesRead += haveLeft), syntaxPartialsSize + haveLeft);

   // if(
      read(
        input, 
        chunk_currentPointer + syntaxPartialsSize,
        chunk_size - syntaxPartialsSize); //== -1
    //){
		//	printf("Error: Unable to read from file.\n");
		//	close(input), close(output);
	//		exit(1);
   // }
  if (syntaxPartialsSize){
    copy(chunk_currentPointer, syntaxPartials, syntaxPartialsSize);
    syntaxPartialsSize = 0;
  }
}
inline const char* BaseStream::chunk_getInitialPointer()const noexcept{
  return chunk_initialPointer;
}
void Worker(
    std::function<BaseStream*()> getStreams
){
  //here writeablePointer is set to initialPointer
  BaseStream* streams = getStreams();
  Timer timer("Worker function");
  if(streams->hasFinished()) goto afterProcess;
	do { // reads chunks
		
    //writeablePointer DOESN'T get rewritten on chunk reads
		streams->readNewChunk();

		do { // checks each chunk
			if (streams->action == streams->Action::None) {
        /*In this branch writablePointer DEFINITELY is defined on entry*/
        char* prefixPointer = streams->chunk_findTemplateSyntax(streams->patternStruct->prefix);

        if(!prefixPointer){
					uint32_t leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
          uint8_t maxLengthOfTemplateSyntax = streams->patternStruct->prefix.size() + streams->patternStruct->maxParamLength;
					if (leftChunkSize > maxLengthOfTemplateSyntax)
            streams->write(streams->chunk_writablePointer, leftChunkSize - maxLengthOfTemplateSyntax);
					streams->copySyntaxPartials(maxLengthOfTemplateSyntax);
					break;
          
        } else {
          // prefixPointer can equal writablePointer ONLY if writeable equals currentPointer
          if(prefixPointer == streams->chunk_writablePointer) streams->chunk_writablePointer = nullptr;
          /* I don't write chunk here due to POTENTIAL "fake prefixes"*/

          streams->chunk_currentPointer = prefixPointer + streams->patternStruct->prefix.size() ;
          
          uint32_t leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
          if(// If there is not enough space left in chunk to get the "action" after "prefix"
             leftChunkSize < streams->patternStruct->maxParamLength 
          ){
            streams->copySyntaxPartials(
                streams->chunk_size - (prefixPointer - streams->chunk_getInitialPointer())
            );
            //Get out and read next chunk
            break;
          } else { 
            // Save it for later
            char* afterActionPointer = streams->chunk_currentPointer;
            // these "if" statements DO NOT exit from this current cycle.
            if(
              !streams->patternStruct->insertOn.empty() 
              && !memcmp(
                streams->chunk_currentPointer,
                streams->patternStruct->insertOn.data(),
                streams->patternStruct->insertOn.length()
              )
            ){
              streams->action = streams->Action::Inserting;
              afterActionPointer+=streams->patternStruct->insertOn.size();
            } else if(!streams->patternStruct->removeOn.empty() 
              && !memcmp(
                streams->chunk_currentPointer,
                streams->patternStruct->removeOn.data(),
                streams->patternStruct->removeOn.length()
              )
            ){
              streams->action = streams->Action::Removing;
              afterActionPointer+=streams->patternStruct->removeOn.size();
            } else if (streams->patternStruct->removeOn.empty()){
              streams->action = streams->Action::Removing;
              afterActionPointer+=streams->patternStruct->removeOn.size();
            } else if (streams->patternStruct->insertOn.empty()){
              streams->action = streams->Action::Inserting;
              afterActionPointer+=streams->patternStruct->insertOn.size();
            } else {
              /*current pointer is AFTER the prefix here*/
              // Prefix is FAKE so I need to mark it as writeable to stream
              if(!streams->chunk_writablePointer) streams->chunk_writablePointer = prefixPointer;
              continue;
            }
            /* here prefix is NOT fake */
            if(streams->chunk_writablePointer) {
              streams->write(
                  streams->chunk_writablePointer,
                  streams->chunk_currentPointer - streams->chunk_writablePointer - streams->patternStruct->prefix.size()
              );
            }
            streams->chunk_currentPointer = afterActionPointer;
            leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
            if(!leftChunkSize) break;
            uint8_t sufficientEndLength = streams->patternStruct->end.size() + 1 /*one byte before end*/;
            if(leftChunkSize < sufficientEndLength){
              streams->copySyntaxPartials(leftChunkSize);
              break;
            } else continue;// restart cycle in appropriate branch
            
          }
        }
      } else if (streams->action == streams->Action::Removing){
	      char* endPointer = streams->chunk_findTemplateSyntax(streams->patternStruct->end);
        if(!endPointer){
          streams->copySyntaxPartials(streams->patternStruct->end.size() - 1);
          break;
        } else {
          streams->action = streams->Action::None;
          streams->chunk_currentPointer = endPointer + streams->patternStruct->end.size();
          uint32_t leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
          if(!leftChunkSize) break;
          else if(leftChunkSize < streams->patternStruct->prefix.size() + streams->patternStruct->maxParamLength){
            streams->copySyntaxPartials(leftChunkSize);
            break;
          } else continue;
        }
      } else if (streams->action == streams->Action::Inserting){
        // chunk_currentPointer DEFINITELY is AFTER action's syntax. It points on insertion indentifier's first byte.
        uint16_t maxAllowedDistanceToEnd = streams->patternStruct->maxSyntaxPartialsSize;
	      char* endPointer = streams->chunk_findTemplateSyntax(
            streams->patternStruct->end,
            maxAllowedDistanceToEnd
        );
        uint32_t leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
        if(!endPointer) {
          if(leftChunkSize < maxAllowedDistanceToEnd){
            streams->copySyntaxPartials(leftChunkSize);
            break;
          } else {
            //TODO error!!!
          }
        } 
        /*TODO: handle case, where end-pointer is SAME as currentPointer OR is 256 and more bytes away OR is absent*/
        streams->insert(streams->chunk_currentPointer, endPointer - streams->chunk_currentPointer);
        streams->chunk_currentPointer = endPointer + streams->patternStruct->end.size();
        leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
        if(!leftChunkSize) break;
        else if(leftChunkSize < streams->patternStruct->prefix.size() + streams->patternStruct->maxParamLength){
          streams->copySyntaxPartials(leftChunkSize);
          break;
        } else continue;
      }
    } while(true);


		if (streams->hasFinished()) {
      if(streams->syntaxPartialsSize)
        streams->write(streams->syntaxPartials, streams->syntaxPartialsSize);
      
      streams->flushChunks();
      break;
    } else streams->flushChunks();

	} while (true);

afterProcess:
  delete streams;
}
