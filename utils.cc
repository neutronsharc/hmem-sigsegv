// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "debug.h"
#include "utils.h"

uint64_t RoundUpToPageSize(uint64_t size) {
  return (size + PAGE_SIZE - 1) & PAGE_MASK;
}

uint64_t NowInUsec() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

bool FSObjectExist(const char* path) {
  struct stat sinfo;
  if (stat(path, &sinfo) < 0) {
    err("The FS object not exist: %s\n", path);
    perror("stat: ");
    return false;
  }
  return true;
}

bool IsDir(const char* path) {
  struct stat sinfo;
  if (stat(path, &sinfo) < 0) {
    err("Unable to stat path: %s\n", path);
    perror("stat: ");
  }
  return S_ISDIR(sinfo.st_mode);
}

bool IsFile(const char* path) {
  struct stat sinfo;
  if (stat(path, &sinfo) < 0) {
    err("Unable to stat path: %s\n", path);
    perror("stat: ");
  }
  return S_ISREG(sinfo.st_mode);
}
