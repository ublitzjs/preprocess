#include <iostream>
#include <cstring>
#include <stdint.h>
#include <optional>
#include <fstream>
#include "./shared.h"
#include <stringzilla/stringzilla.hpp>

#define copy memcpy



#if PLATFORM_APPROACH == 0
#include <Windows.h>
#elif PLATFORM_APPROACH == 1
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#else 
#include <fstream>
#endif

namespace globals {
	bool removingCode = false;
	char* initialChunkPointer;
}
class ChunkHelper {
	friend class Streams;
	char* pointer = globals::initialChunkPointer;
	uint32_t size;
	char* additionalBuffer;
	uint8_t additionalBufferSize = 0;
public:	
	~ChunkHelper() {
      delete[] additionalBuffer;
	}
	inline char* movePointerForward(uint16_t bytesDistance) noexcept {
		return pointer += bytesDistance;
	}
	inline uint16_t movedDistance() const noexcept {
		return pointer - globals::initialChunkPointer;
	}
	inline int64_t findPreprocessorIndex(const Preprocessor* preprocessor) const noexcept {
		const char* result = sz_find(pointer, size - movedDistance(), preprocessor->txt, preprocessor->sizeWithout1byte + 1);
		if (!result) return -1;
		return result - pointer;
	}
	inline uint8_t getAdditionalBufferSize() const noexcept {
		return additionalBufferSize;
	}
	inline uint32_t getSize() const noexcept {
		return size;
	}
	char* getPointer() const noexcept {
		return pointer;
	}
	inline char* getAddBuffer() const noexcept {
		return additionalBuffer;
	}
	inline void copyAddBuffer(uint32_t have_left, uint8_t copySize) {
		additionalBufferSize = have_left >= copySize ? copySize : have_left;
		copy(
			additionalBuffer,
			globals::initialChunkPointer + size - additionalBufferSize,
			additionalBufferSize
		);
	}
};

class Streams {
private:
	unsigned long long inputFileSize = 0, bytes_read = 0;
public:
	bool hasFinished() {
		return inputFileSize == bytes_read;
	}
#if PLATFORM_APPROACH == 0
private:
	HANDLE input;
	HANDLE output;
public:
	Streams(const ProcessRequirements* reqs, ChunkHelper& chunk) {
		input = CreateFile(
			reqs->inputFilename,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);
		if (input == INVALID_HANDLE_VALUE) {
			std::cout << "Couldn't open file " << reqs->inputFilename;
			exit(1);
		}
		inputFileSize = GetFileSize(input, NULL);

		output = CreateFile(
			reqs->outputFilename,
			GENERIC_WRITE,
			FILE_SHARE_WRITE,
			NULL,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);
		if (output == INVALID_HANDLE_VALUE) {
			std::cout << "Couldn't create output file " << reqs->inputFilename;
			CloseHandle(input);
			exit(1);
		}
		chunk.additionalBuffer = new char[
			reqs->startPreprocessor->sizeWithout1byte > reqs->endPreprocessor->sizeWithout1byte
				? reqs->startPreprocessor->sizeWithout1byte
				: reqs->endPreprocessor->sizeWithout1byte
		];
	}
	~Streams() {
		CloseHandle(input), CloseHandle(output);
		globals::removingCode = false;
	}
	void write(const char* source, uint32_t size) {
		if (WriteFile(output, source, size, nullptr, NULL) == FALSE) {
			printf("Error: unable to write to file.\n GetLastError=%08x\n", GetLastError());
			CloseHandle(input);
			CloseHandle(output);
			exit(1);
		}
	}	
	inline void readNewChunk(ChunkHelper& chunk) {
		unsigned long long have_left = inputFileSize - bytes_read;

		chunk.pointer = globals::initialChunkPointer;
		chunk.size = chunk.additionalBufferSize + have_left >= maxChunk
			? ((bytes_read += maxChunk - chunk.additionalBufferSize), maxChunk)
			: ((bytes_read += have_left), chunk.additionalBufferSize + have_left);

		if (ReadFile(
			input,
			chunk.pointer + chunk.additionalBufferSize,
			chunk.size - chunk.additionalBufferSize,
			nullptr,
			NULL
		) == FALSE) {
			printf("Error: Unable to read from file.\n GetLastError=%08x\n", GetLastError());
			CloseHandle(input);
			CloseHandle(output);
			exit(1);
		}

		if (chunk.additionalBufferSize)
			copy(chunk.pointer, chunk.additionalBuffer, chunk.additionalBufferSize);

		chunk.additionalBufferSize = 0;
	}
#elif PLATFORM_APPROACH == 1
private:
  int input;
  int output;
public:
  Streams(const ProcessRequirements* reqs, ChunkHelper& chunk){
    input = open(reqs->inputFilename, O_RDONLY);
    if(input == -1){
			std::cout << "Couldn't open file " << reqs->inputFilename;
      exit(1);
    }
    output = open(reqs->outputFilename, O_WRONLY | O_TRUNC | O_CREAT );
    if(output == -1){
			std::cout << "Couldn't create output file " << reqs->outputFilename;
      close(input);
      exit(1);
    }
    { // get file's size
      struct stat inputStats;
      if(!fstat(input, &inputStats))
        inputFileSize = inputStats.st_size;
      else {
        std::cout<<"Couldn't get size of \"" << reqs->inputFilename << "\"";
        exit(1);
      }; 
    }
		chunk.additionalBuffer = new char[
			reqs->startPreprocessor->sizeWithout1byte > reqs->endPreprocessor->sizeWithout1byte
				? reqs->startPreprocessor->sizeWithout1byte
				: reqs->endPreprocessor->sizeWithout1byte
		];
  }
  ~Streams(){
    std::cout<<"Finishing: "<<input<<' '<<output<<'\n';
    close(input), close(output);
    globals::removingCode = false;
  }
  void write(const char* source, uint32_t size){
    if(::write(output, source, size) == -1) {
			printf("Error: unable to write to file.");
			close(input),close(output);
			exit(1);
    };
  }
	inline void readNewChunk(ChunkHelper& chunk) {
		unsigned long long have_left = inputFileSize - bytes_read;

		chunk.pointer = globals::initialChunkPointer;
		chunk.size = chunk.additionalBufferSize + have_left >= maxChunk
			? ((bytes_read += maxChunk - chunk.additionalBufferSize), maxChunk)
			: ((bytes_read += have_left), chunk.additionalBufferSize + have_left);
    if(
      read(
        input, 
        chunk.pointer + chunk.additionalBufferSize,
        chunk.size - chunk.additionalBufferSize
      ) == -1
    ){
			printf("Error: Unable to read from file.\n");
			close(input), close(output);
			exit(1);
    }

		if (chunk.additionalBufferSize)
			copy(chunk.pointer, chunk.additionalBuffer, chunk.additionalBufferSize);

		chunk.additionalBufferSize = 0;
  }
#else
private:
	std::ifstream input;
	std::ofstream output;
public:
	Streams(const ProcessRequirements * reqs, ChunkHelper & chunk) {
		input.open(reqs->inputFilename, std::ios::in | std::ios::binary);
		if (input.fail()) {
			std::cout << "Couldn't open file " << reqs->inputFilename;
			exit(1);
		}

		// get file size
		input.seekg(0, std::ios::end);
		inputFileSize = input.tellg();
		input.seekg(0, std::ios::beg);

		output.open(reqs->outputFilename, std::ios::out | std::ios::binary);
		if (output.fail()) {
			input.close();
			std::cout << "Couldn't start writing to file " << reqs->outputFilename;
			exit(1);
		}

		if (inputFileSize == 0) {
			input.close(), output.close();
			std::cout << "Input file is empty";
			exit(0);
		}
		chunk.additionalBuffer = new char[
			reqs->startPreprocessor->sizeWithout1byte > reqs->endPreprocessor->sizeWithout1byte
				? reqs->startPreprocessor->sizeWithout1byte
				: reqs->endPreprocessor->sizeWithout1byte
		];
	}
	~Streams() {
		input.close();
		output.close();
		globals::removingCode = false;
	}
	void write(const char* destination, uint32_t size) {
		output.write(destination, size);
	}
	inline void readNewChunk(ChunkHelper & chunk) {
		unsigned long long have_left = inputFileSize - bytes_read;

		chunk.pointer = globals::initialChunkPointer;
		chunk.size = chunk.additionalBufferSize + have_left >= maxChunk
			? ((bytes_read += maxChunk - chunk.additionalBufferSize), maxChunk)
			: ((bytes_read += have_left), chunk.additionalBufferSize + have_left);

		input.read(
			chunk.pointer + chunk.additionalBufferSize,
			chunk.size - chunk.additionalBufferSize
		);

		if (chunk.additionalBufferSize)
			copy(chunk.pointer, chunk.additionalBuffer, chunk.additionalBufferSize);

		chunk.additionalBufferSize = 0;
	}
#endif
};

static void minifyFile(const ProcessRequirements* reqs) {
	ChunkHelper chunk;
	Timer timer("minifying single file");
	Streams streams(reqs, chunk);
	if (streams.hasFinished()) return;
	do { // reads chunks
		
		streams.readNewChunk(chunk);

		do { // checks each chunk
			if (globals::removingCode) {
				int64_t index = chunk.findPreprocessorIndex(reqs->endPreprocessor);
				if (index != -1) {
					globals::removingCode = false;
					chunk.movePointerForward(index + reqs->endPreprocessor->sizeWithout1byte + 1);
					if (chunk.movedDistance() == chunk.getSize()) break;
					continue;
				}
				else {
					chunk.copyAddBuffer(chunk.getSize() - chunk.movedDistance(), reqs->endPreprocessor->sizeWithout1byte);
					break;
				}
			}
			else {
				int64_t index = chunk.findPreprocessorIndex(reqs->startPreprocessor);
				if (index != -1) {
					streams.write(chunk.getPointer(), index);
					globals::removingCode = true;
					chunk.movePointerForward(index + reqs->startPreprocessor->sizeWithout1byte + 1);
					if (chunk.movedDistance() == chunk.getSize()) break;
					continue;
				}
				else {
					// can't be zero. I am sure.
					uint32_t have_left = chunk.getSize() - chunk.movedDistance();
					if (have_left > reqs->startPreprocessor->sizeWithout1byte)
						streams.write(chunk.getPointer(), have_left - reqs->startPreprocessor->sizeWithout1byte);
					chunk.copyAddBuffer(have_left, reqs->startPreprocessor->sizeWithout1byte);
					break;
				}
			}
		} while (true);


		if (streams.hasFinished())break;

	} while (true);
	if (!globals::removingCode && chunk.getAdditionalBufferSize())	streams.write(chunk.getAddBuffer(), chunk.getAdditionalBufferSize());
}

void minifyFiles(std::vector<ProcessRequirements, std::allocator<ProcessRequirements>>& allReqs) {
	Timer timer("loop of minifying files");
	globals::initialChunkPointer = new char[maxChunk];
	for (const ProcessRequirements& req : allReqs)
		minifyFile(&req);
	delete[] globals::initialChunkPointer;
}


