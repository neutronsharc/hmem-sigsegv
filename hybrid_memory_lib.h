#ifndef HYBRID_MEMORY_LIB_H_
#define HYBRID_MEMORY_LIB_H_

#include <stdint.h>
#include <string>

bool InitHybridMemory(const std::string& ssd_dirpath,
                      const std::string& hmem_group_name,
                      uint64_t page_buffer_size,
                      uint64_t ram_buffer_size,
                      uint64_t ssd_buffer_size,
                      uint32_t number_hmem_instance);

void ReleaseHybridMemory();

void *hmem_alloc(uint64_t size);

void hmem_free(void *address);

uint64_t GetNumberOfPageFaults();

#endif  // HYBRID_MEMORY_LIB_H_
