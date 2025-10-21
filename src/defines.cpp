#include "./include/shared2.hpp"
std::vector<syntax::dataStruct> syntax::dataVector; 
std::mutex caching::dataMapMutex;
std::map<std::string, caching::dataStruct> caching::dataMap;
uint32_t maxChunkSize = 64*1024;
ThreadPools caching::workers;
ThreadPools streaming::workers;
