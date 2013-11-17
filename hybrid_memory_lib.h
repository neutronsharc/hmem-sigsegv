// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef HYBRID_MEMORY_LIB_H_
#define HYBRID_MEMORY_LIB_H_

#include <stdint.h>
#include <string>


struct V2HMapMetadata;
class VAddressRange;

bool InitHybridMemory(const std::string& ssd_dirpath,
                      const std::string& hmem_group_name,
                      uint64_t page_buffer_size,
                      uint64_t ram_buffer_size,
                      uint64_t ssd_buffer_size,
                      uint32_t number_hmem_instance);

void ReleaseHybridMemory();

void* hmem_alloc(uint64_t size);

// Map a region of disk file into virtual-memory.
// The region starts from "file_offset" and covers "size" bytes.
void* hmem_map(const std::string& hdd_filename,
               uint64_t size,
               uint64_t hdd_file_offset);

void hmem_free(void *address);

uint64_t NumberOfPageFaults();

uint64_t FoundPages();

uint64_t UnFoundPages();

uint64_t GetPageOffsetInVAddressRange(uint32_t vaddress_range_id, void* page);

V2HMapMetadata* GetV2HMap(uint32_t vaddress_range_id, uint64_t page_offset);

VAddressRange* GetVAddressRangeFromId(uint32_t vaddress_range_id);

void HybridMemoryStats();

#endif  // HYBRID_MEMORY_LIB_H_
