#pragma once
#include <stdint.h>
#if PLATFORM_APPROACH == 0
#include <Windows.h>
namespace cross_os {
  using descriptor_t = HANDLE;
  extern descriptor_t invalid_descriptor;
  inline descriptor_t OpenFileRead(const char* str) {
    return CreateFile(str,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  }
  inline descriptor_t OpenFileWrite(const char* str){
    return CreateFile(str, GENERIC_WRITE, FILE_SHARE_WRITE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
  }
  inline void CloseDescriptor(descriptor_t descriptor){
    CloseHandle(descriptor);
  }
  constexpr int8_t invalid_file_size = -1;
  inline int64_t GetFileSize(descriptor_t descriptor){
    uint64_t fileSize = ::GetFileSize(descriptor, NULL);
    if (fileSize == INVALID_FILE_SIZE && GetLastError() != NO_ERROR){
      return -1;
    };
    return fileSize; 
  }
  constexpr int8_t ReadFailed = 0;
  inline int64_t ReadFile(descriptor_t descriptor, void* destination, uint64_t size){
    return ::ReadFile(
      descriptor,
      destination,
      size,
			nullptr,
			NULL
		);
  }
}
#elif PLATFORM_APPROACH == 1
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
namespace cross_os {
  using descriptor_t = int;
  extern descriptor_t invalid_descriptor;
  inline descriptor_t OpenFileRead(const char* str) {
    return open(str, O_RDONLY);
  }
  inline descriptor_t OpenFileWrite(const char* str){
    return open(str, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  }
  inline void CloseDescriptor(descriptor_t descriptor){
    close(descriptor);
  }
  constexpr int8_t invalid_file_size = -1;
  constexpr int8_t ReadFailed = -1;
  inline int64_t GetFileSize(descriptor_t descriptor){
    struct stat inputStats;
    if (fstat(descriptor, &inputStats)) {
      return -1;
    };
    return inputStats.st_size;
  }
  inline int64_t ReadFile(descriptor_t descriptor, void* destination, uint64_t size){
    return read(descriptor, destination, size);

  }
}
#else 
#include <fstream>
#endif
