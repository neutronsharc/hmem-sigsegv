// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include "utils.h"

uint64_t RoundUpToPageSize(uint64_t size) {
  return (size + PAGE_SIZE - 1) & PAGE_MASK;
}
