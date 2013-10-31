#ifndef UTILS_H_
#define UTILS_H_

#include <stdint.h>
#include "hybrid_memory_const.h"

// Round up a value to page alignment.
uint64_t RoundUpToPageSize(uint64_t size);

#endif  // UTILS_H_
