#include <iostream>
#include <cstring>
#include <fstream>
#include <string_view>
#include <optional>
#include <stdlib.h>
#include <stdint.h>
#include <thread>
#include <string>
#include <filesystem>
#include <vector>
#include <regex>
#include "./shared.h"
#include <stringzilla/stringzilla.hpp>

namespace fs = std::filesystem;

struct PreprocessorPattern {
	std::regex regexp;
	Preprocessor* preprocessor;
	PreprocessorPattern(std::regex regexp, Preprocessor* preprocessor)
		: regexp(regexp), preprocessor(preprocessor){}
};

static const Preprocessor* startPrep = new Preprocessor{ "/*_START_DEV_*/", 15 - 1 };
static const Preprocessor* endPrep = new Preprocessor{ "/*_END_DEV_*/", 13 - 1 };

//static uint8_t cores = std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1;

static std::vector<PreprocessorPattern> startPatterns, endPatterns;
static std::vector<ProcessRequirements, std::allocator<ProcessRequirements>> allReqs;
static std::optional<std::regex> avoidRegex = {};

// always ends on slash. (or / or \ )
static std::string outdir = "./";
static std::string mode = "files"; 

namespace setup {
	namespace build {
		static void setOutdir(const char* argument) {
			uint16_t length = strlen(argument);
			if (length == outDirArgLength) {
				std::cerr << "Argument \"" << argument << "\" doesn't provide any value.\n";
				exit(1);
			}
			outdir = argument + outDirArgLength;
			if (argument[length - 1] != '/') outdir+='/';
		}
		/*static void setMaxCores(const char* argument) {
			uint16_t length = strlen(argument);
			if (length == maxCoresArgLength) {
				std::cerr << "Not full --max-cores argument";
				exit(1);
			}
			cores = std::stoi(std::string(argument).substr(length - 1));
		}*/
		static void setMode(const char* changedMode) {
			mode = changedMode;
		}
		static void addPreprocessorPattern(
			const char* argument, 
			uint8_t patternArgLength, 
			std::vector<PreprocessorPattern>& currentPreprocessorPatterns
		) {
			uint8_t length = strlen(argument);
			if (length == patternArgLength) {
				std::cerr << "Argument "<< argument << " has no value.\n";
				exit(1);
			}
			const char* argumentAdjacentPointer = argument;
			argumentAdjacentPointer += patternArgLength + 1;
			length -= patternArgLength + 1;

			if(
				// --pattern-s=qwerty
				*(argumentAdjacentPointer - 1) != '/'
				// --pattern-s=//:qwerty
				|| *(argumentAdjacentPointer) == '/'
			) {
				std::cerr << "Argument's \""<<argument << "\" regex part doesn't start from slash OR has 2 slashes in a row.\n";
				exit(1);
			}
			uint8_t regexLength;
			{
				const char* endRegexPointer = sz_find(argumentAdjacentPointer, length, "/:", 2);
				if (!endRegexPointer) {
					std::cerr << "Argument's \""<<argument<<"\" regex part doesn't end with /: symbols.\n";
					exit(1);
				}
				regexLength = endRegexPointer - argumentAdjacentPointer;
			}

			std::regex regexp(std::string(argumentAdjacentPointer, regexLength));
			argumentAdjacentPointer += /*skip first slash*/ regexLength + 2 /*skip /: */;
			length -= regexLength + 2;
			if (!length) {
				std::cerr << "Argument \"" << argumentAdjacentPointer << "\" doesn't provide any pattern-text.\n";
				exit(1);
			}
			char* preprocessorTxt = new char[length + 1];
			preprocessorTxt[length] = '\0';
			memcpy(preprocessorTxt, argumentAdjacentPointer, length);
			Preprocessor* preprocessor = new Preprocessor{ preprocessorTxt, static_cast<uint8_t>(length - 1) };
			currentPreprocessorPatterns.emplace_back(regexp, preprocessor);
		}
		static void setAvoidRegex(const char* argument) {
			if (strlen(argument) == avoidArgLength) {
				std::cerr << "argument --avoid has no value provided";
				exit(1);
			}
			avoidRegex = argument + avoidArgLength;
		}
		static void setDefaultPreprocessor(const char* argument, bool isStartingPreprocessor) {
			uint8_t length = strlen(argument);
			if (isStartingPreprocessor) {
				if (length == setStartPrepArgLength) {
					std::cerr << "argument " << setStartPrepArgSyntax << " has no value\n";
					exit(1);
				}
				delete startPrep;
				startPrep = new Preprocessor{ argument + setStartPrepArgLength, static_cast<uint8_t>(length - setStartPrepArgLength - 1) };
			}
			else {
				if (length == setEndPrepArgLength) {
					std::cerr << "argument " << setEndPrepArgSyntax << " has no value\n";
					exit(1);
				}
				delete endPrep;
				endPrep = new Preprocessor{ argument + setEndPrepArgLength, static_cast<uint8_t>(length - setEndPrepArgLength - 1) };
			}
		}
	}
	namespace fill_reqs {
		static const Preprocessor* findPreprocessorUsingPattern(const char* filename, std::vector<PreprocessorPattern>& patterns){
			for (PreprocessorPattern& pattern : patterns) {
				std::cmatch matches;
				if (std::regex_search(filename, matches, pattern.regexp))
					return pattern.preprocessor;
			}
			return nullptr;
		}
		static void iterateDir(std::string&& dirString, uint8_t skipDirStringChars, std::string& outDirString) {
			for (const fs::directory_entry& entry : fs::directory_iterator{ dirString }) {
				if (avoidRegex.has_value()) {
					std::cmatch matches;
					if (std::regex_search(
						entry.path().string().c_str(),
						matches,
						avoidRegex.value()
					)) continue;
				}
				if (entry.is_directory()) 
					iterateDir(entry.path().string() + '/', skipDirStringChars, outDirString);
				else {
					char* startCstring = new char[entry.path().string().size() + 1];
					memcpy(startCstring, entry.path().string().c_str(), entry.path().string().size() + 1);
					std::string newOutPath = outDirString + (entry.path().string().c_str() + skipDirStringChars);
					char* endCstring = new char[newOutPath.size() + 1];
					memcpy(endCstring, newOutPath.c_str(), newOutPath.size() + 1);
					const Preprocessor* startPreprocessor = fill_reqs::findPreprocessorUsingPattern(startCstring, startPatterns);
					const Preprocessor* endPreprocessor = fill_reqs::findPreprocessorUsingPattern(startCstring, endPatterns);
					if (!startPreprocessor) startPreprocessor = startPrep;
					if (!endPreprocessor) endPreprocessor = endPrep;
					allReqs.emplace_back(startCstring, endCstring, startPreprocessor, endPreprocessor);
				}
			}
		}
		static void fromDir(std::string argument) {
			int16_t in_out_separator = argument.find('|');
			if (
				// has to be this:  "<filename.txt>" or "<filename.txt|out.txt>"
				argument[0] != '<' || argument[argument.size() - 1] != '>'
				// exclude "<|xxxx>" and "<xxxx|>
				|| in_out_separator == 1 || in_out_separator == argument.size() - 2
				) {
				std::cerr << "Argument \"" << argument << "\" is of wrong format";
				exit(1);
			}
			bool userGaveOutput = in_out_separator != -1;

			std::string dirString = argument.substr(1, userGaveOutput ? in_out_separator - 1 : argument.size() - 2);
			if (dirString[dirString.size() - 1] != '/' && dirString[dirString.size() - 1] != '\\') dirString += '/';
			std::string outDirString = outdir;
			if (userGaveOutput)	{
				std::string currentOutDirString = argument.substr(in_out_separator + 1, argument.size() - in_out_separator - 2);
				if (currentOutDirString[currentOutDirString.size() - 1] != '/' || currentOutDirString[currentOutDirString.size() - 1] != '\\')
					currentOutDirString += '/';
				outDirString = currentOutDirString;
			}

			iterateDir(std::move(dirString), dirString.size(), outDirString);
		}
		static void fromFiles(std::string argument) {
			int16_t in_out_separator = argument.find('|');
			if (
				// has to be this:  "<filename.txt>" or "<filename.txt|out.txt>"
				argument[0] != '<' || argument[argument.size() - 1] != '>'
				// exclude "<|xxxx>" and "<xxxx|>
				|| in_out_separator == 1 || in_out_separator == argument.size() - 2
				) {
				std::cerr << "Argument \"" << argument << "\" is of wrong format";
				exit(1);
			}
			bool userGaveOutput = in_out_separator != -1;
			char* startCstring; char* endCstring;
			{
				const std::string startString = argument.substr(
					1,
					userGaveOutput ? in_out_separator - 1 : argument.size() - 2
				);
				std::cmatch matches;
				if (avoidRegex.has_value() && std::regex_search(startString.c_str(), matches, avoidRegex.value())) return;
				startCstring = new char[startString.size() + 1];
				memcpy(startCstring, startString.c_str(), startString.size() + 1);
			}
			const Preprocessor* startPreprocessor = fill_reqs::findPreprocessorUsingPattern(startCstring, startPatterns);
			const Preprocessor* endPreprocessor = fill_reqs::findPreprocessorUsingPattern(startCstring, endPatterns);
			if (!startPreprocessor) startPreprocessor = startPrep;
			if (!endPreprocessor) endPreprocessor = endPrep;
			if (userGaveOutput) {
				const std::string startString = argument.substr(1, in_out_separator - 1);
				const std::string endString = argument.substr(in_out_separator + 1, argument.size() - in_out_separator - 2);
				memcpy(startCstring, startString.c_str(), startString.size() + 1);
				endCstring = new char[endString.size() + 1];
				memcpy(endCstring, endString.c_str(), endString.size() + 1);
			}
			else {
				const std::string startString = argument.substr(1, argument.size() - 2);
				memcpy(startCstring, startString.c_str(), startString.size() + 1);
				fs::path filename = fs::path(startString).filename();
				const std::string endString = outdir + filename.string();
				endCstring = new char[endString.length() + 1];
				memcpy(endCstring, endString.c_str(), endString.size() + 1);
			}
			allReqs.emplace_back(startCstring, endCstring, startPreprocessor, endPreprocessor);
		}
	}
	static void main(int argc, const char* const argv[]) {
		Timer timer("setup function");
		uint32_t argI = 0;

#define is_equal !memcmp

		// check everything BEFORE files
		while(++argI < argc) {
			const char* argument = argv[argI];
			// --outdir=
			if (is_equal(argument, outDirArgSyntax, outDirArgLength)) build::setOutdir(argument);

			// --files: --dirs:
			else if (is_equal(argument, filesStartFlagSyntax, filesStartFlagLength)) break;
			else if (is_equal(argument, dirsStartFlagSyntax, dirsStartFlagLength)) {
				build::setMode(dirsMode);
				break;
			}

			// --pattern-s=/regex/:PATTERN
			else if (is_equal(argument, startPatternArgSyntax, startPatternArgLength))
				build::addPreprocessorPattern(argument, startPatternArgLength, startPatterns);
			else if (is_equal(argument, endPatternArgSyntax, endPatternArgLength))
				build::addPreprocessorPattern(argument, endPatternArgLength, endPatterns);

			// --avoid=
			else if (is_equal(argument, avoidArgSyntax, avoidArgLength)) build::setAvoidRegex(argument);

			// --default-s=
			else if (is_equal(argument, setStartPrepArgSyntax, setStartPrepArgLength)) build::setDefaultPreprocessor(argument, true);
			else if (is_equal(argument, setEndPrepArgSyntax, setEndPrepArgLength)) build::setDefaultPreprocessor(argument, false);

			//else if (!memcmp(argument, maxCoresArgSyntax, maxCoresArgLength)) build::setMaxCores(argument);

			else {
				std::cerr << "Option \"" << argument << "\" can't be used OR can't be used BEFORE "
					<< dirsStartFlagSyntax << " or " << filesStartFlagSyntax;
				exit(1);
			}
		}

		if (argI >= argc-1) {
			std::cerr << "You have to include a flag signifying the beginning of files/dirs (--files: / --dirs:) AND the files/dirs";
			exit(1);
		}

		// check FILES or DIRS
		if (mode == dirsMode)  while (++argI < argc) fill_reqs::fromDir(argv[argI]);
		else while (++argI < argc) fill_reqs::fromFiles(argv[argI]);
	}
}


/*2)				
	.exe
	"--outdir=D://hello world"
	--dirs: 
	"<C://dev-folder>"
*/

#define CLI_WORKS 1

int main(
#if CLI_WORKS
	 int argc, const char* const argv[]
#endif 
) {
#if !CLI_WORKS
	int argc = 6;
	const char* const argv[6] = { ".exe","--pattern-s=/2/:MY START ", "--outdir=./", "--pattern-e=/2/:MY END", "--dirs:",  "<x64/Release/samples|here>" };
#endif

	if (argc == 1) {
		std::cout << R"(
This is an executable for removing unwanted code from production.
Just like macro in C++, but only "#if 0". 
In your target file you mark some part with a "starting phrase" and "ending phrase", 
which you can manually specify. This exe removes these phrases and everything in between.
Use it for dynamic swagger specifications, logs to console, debugging imports and more.
1) Arguments
1.1) Order: setting flags -> flag singnifying targets -> targets;
  !setting flags are optional
1.2) Setting flags:
  
  1.2.1) --outdir=PATH
  All targets with unspecified output next to them will use PATH for outputs.
  PATH can be absolute and relative.
  Default - ./  (current working directory)
  Example: --outdir=../almost-dist
  
  1.2.2) --avoid=REGEXP
  If some targets match the REGEXP (don't give slashes) - they 
  won't be processed
  Example: --avoid=.git|.dockerignore|.conf
  
  1.2.3) --default-s=START_PHRASE
  IF you don't give it, /*_START_DEV_*/ will be used
  Example for python target: "--default-s=#START DEV"
  
  1.2.4) --default-e=END_PHRASE
  This exe used /*_END_DEV_*/ by its default
  Example for lua: "--default-e=--finish of development"

  1.2.5) --pattern-s=/FILE_REGEXP/:START_PHRASE
  If target matches FILE_REGEXP between slashes, then it will use 
  START_PHRASE instead of the default one.
  This option can be used several times.
  Example: --pattern-s=/.py/:#START_DEV
  
  1.2.6) --pattern-e=/FILE_REGEXP/:END_PHRASE
  Same as for start phrase.
  Example: "--pattern-e=/.ts/:/*My own end*/"

1.3) flags before targets AND targets
  1.3.1) --files:
  This means that from now on you will give ONLY files to process.
  Target files (relative or absolute) must be passed between "<>" signs.
  
  Example 1: --outdir=./almost-dist --files: <input.txt>
  In this example "input.txt" will become "almost-dist/input.txt"
  If you don't give --outdir, this example would try to create 
  another input.txt in ./ directory, resulting in an error.
  
  But you can give it your own output name using | sign
  Example 2: --files: "<input.txt|output.txt>"
  This overrides --outdir

  You can feed this exe with many files
  Example 3: --outdir=./dist --files: <one.txt> <D://two.txt> <three.txt|D://out.txt> 

  1.3.2) --dirs:
  This instructs exe to recursively process directories.
  Format and use cases are the same as of files.

  Example 1: --dirs: <input-dir> <dir2|outdir> 

2) For full examples go to github page
)";
		return 0;
	} else if (argc == 2) {
		std::cerr << "You should pass at least 2 arguments";
		return 1;
	}

	setup::main(argc, argv);

	if (!allReqs.size()) {
		std::cout << "Seems like --avoid regex matches all files and neither could be processed\n";
		return 0;
	};

	minifyFiles(allReqs);

	return 0;
}
