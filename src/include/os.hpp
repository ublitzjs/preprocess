#if PLATFORM_APPROACH == 0
#include <Windows.h>
#elif PLATFORM_APPROACH == 1
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#else 
#include <fstream>
#endif
