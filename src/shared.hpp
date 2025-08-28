#pragma once
#include <stdint.h>
#include <vector>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <filesystem>
namespace fs = std::filesystem;
struct Preprocessor {
	const char* txt;
	uint8_t sizeWithout1byte;
};
constexpr uint8_t MIN_ARGC = 3; // first - useless path to .exe file
constexpr const char* const START_TAG = "/*_START_DEV_*/";
constexpr const uint8_t START_TAG_LENGTH = 15;
constexpr const char* const END_TAG = "/*_END_DEV_*/";
constexpr const uint8_t END_TAG_LENGTH = 13;

constexpr const char* maxCoresArgSyntax = "--max-cores=";
constexpr const uint8_t maxCoresArgLength = 12;

constexpr const char* outDirArgSyntax = "--outdir=";
constexpr const uint8_t outDirArgLength = 9;

constexpr const char* startPatternArgSyntax = "--pattern-s=";
constexpr const uint8_t startPatternArgLength = 12;
constexpr const char* endPatternArgSyntax = "--pattern-e=";
constexpr const uint8_t endPatternArgLength = 12;


constexpr const char* filesStartFlagSyntax = "--files:";
constexpr const uint8_t filesStartFlagLength = 8;
constexpr const char* filesMode = "files";
constexpr const char* dirsStartFlagSyntax = "--dirs:";
constexpr const uint8_t dirsStartFlagLength = 7;
constexpr const char* dirsMode = "dirs";

constexpr const char* avoidArgSyntax = "--avoid=";
constexpr const uint8_t avoidArgLength = 8;

constexpr const char* setStartPrepArgSyntax = "--default-s=";
constexpr const uint8_t setStartPrepArgLength = 12;

constexpr const char* setEndPrepArgSyntax = "--default-e=";
constexpr const uint8_t setEndPrepArgLength = 12;


#ifdef DEBUG_CHUNK_SIZE
constexpr const uint16_t maxChunk = DEBUG_CHUNK_SIZE;
#else
constexpr const uint32_t maxChunk = UINT16_MAX * 8;
#endif

class Timer {
private:
	std::chrono::time_point<std::chrono::high_resolution_clock> start_Timepoint;
	const char* subject;
	bool logged = false;
public:
	Timer(const char* subject)
		: start_Timepoint(std::chrono::high_resolution_clock::now()), subject(subject)
	{
	}
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


struct ProcessRequirements {
	const char* inputFilename;
	const char* outputFilename;
	const Preprocessor* startPreprocessor;
	const Preprocessor* endPreprocessor;
	ProcessRequirements(
		const char* inputFilename,
		const char* outputFilename,
		const Preprocessor* startPreprocessor,
		const Preprocessor* endPreprocessor
	)
		: inputFilename(inputFilename), outputFilename(outputFilename), startPreprocessor(startPreprocessor), endPreprocessor(endPreprocessor)
	{
		fs::path outputPath = outputFilename;
		if (outputPath.has_parent_path()) {
			fs::path parentPath = outputPath.parent_path();
			if (!fs::exists(parentPath)) fs::create_directories(parentPath);
		}
	}
};

void minifyFiles(std::vector<ProcessRequirements, std::allocator<ProcessRequirements>>& allReqs);
