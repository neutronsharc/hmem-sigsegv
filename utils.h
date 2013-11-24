// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef UTILS_H_
#define UTILS_H_

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "hybrid_memory_const.h"

// Round up a value to page alignment.
uint64_t RoundUpToPageSize(uint64_t size);

// Get current time in micro-seconds.
uint64_t NowInUsec();

bool FSObjectExist(const char* path);

bool IsDir(const char* path);

bool IsFile(const char* path);

#endif  // UTILS_H_
