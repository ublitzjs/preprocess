#include "./include/shared2.hpp"
#include "./include/cross_os.hpp"
std::vector<syntax::dataStruct> syntax::dataVector; 
tbb::concurrent_hash_map<std::string, caching::dataStruct*> caching::dataMap;
tbb::concurrent_unordered_map<caching::dataStruct*, std::vector<streaming::dataStruct*>> streaming::cacheDependentTasks;
uint32_t maxChunkSize = 64*1024;
ThreadPools streaming::workers;
#if PLATFORM_APPROACH == 0
cross_os::descriptor_t cross_os::invalid_descriptor_t = INVALID_HANDLE_VALUE;
#elif PLATFORM_APPROACH == 1
cross_os::descriptor_t cross_os::invalid_descriptor_t = -1;
#endif
