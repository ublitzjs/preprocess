#include "./include/shared2.hpp"
#include "./include/cross_os.hpp"
std::vector<syntax::dataStruct> syntax::dataVector; 
std::mutex caching::dataMapMutex;
std::map<std::string, caching::dataStruct> caching::dataMap;
uint32_t maxChunkSize = 64*1024;
ThreadPools caching::workers;
ThreadPools streaming::workers;
#if PLATFORM_APPROACH == 0
cross_os::descriptor_t cross_os::invalid_descriptor_t = INVALID_HANDLE_VALUE;
#elif PLATFORM_APPROACH == 1
cross_os::descriptor_t cross_os::invalid_descriptor_t = -1;
#endif
