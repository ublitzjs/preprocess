#include "./include/shared3.hpp"
#include "./include/cross_os.hpp"
uint32_t maxChunkSize = 64*1024;
#if PLATFORM_APPROACH == 0
cross_os::descriptor_t cross_os::invalid_descriptor = INVALID_HANDLE_VALUE;
#elif PLATFORM_APPROACH == 1
cross_os::descriptor_t cross_os::invalid_descriptor = -1;
#endif
