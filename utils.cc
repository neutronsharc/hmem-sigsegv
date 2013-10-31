#include "utils.h"

uint64_t RoundUpToPageSize(uint64_t size) {
  return (size + PAGE_SIZE - 1) & PAGE_MASK;
}
