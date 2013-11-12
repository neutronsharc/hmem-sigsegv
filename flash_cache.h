#ifndef FLASH_CACHE_H_
#define FLASH_CACHE_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue>
#include "hash_table.h"
#include "lru_list.h"
#include "free_list.h"
#include "page_allocation_table.h"
#include "page_stats_table.h"

struct V2HMapMetadata;
class HybridMemory;

// Assume no more than 255 vaddress-ranges.
#define INVALID_VADDRESS_RANGE_ID (0xff)

// Flash-to-virtual-address mapping metadata for each flash-page.
struct F2VMapItem {
  // Hosting vaddr-range id of the virtual-page cached in this flash-page.
  uint32_t vaddress_range_id : 8;
  // The cached virtual-pages's page-offset in the hosting vaddr-range.
  uint32_t vaddress_page_offset : 24;
} __attribute__((__packed__));

// Flash cache is the 3rd layer of cache that stores all
// pages that overflows from 2st layer (RAM-cache).
class FlashCache {
 public:
  FlashCache() : ready_(false), hybrid_memory_(NULL), f2v_map_(NULL) {}

  virtual ~FlashCache() { Release(); }

  bool Init(HybridMemory* hmem,
            const std::string& name,
            const std::string& flash_filename,
            uint64_t max_flash_size);

  void Release();

  // Save a virtual-page that's overflowed from the upper RAM-cache layer.
  // "data" is the real-memory that contains the data copy.
  // "is_dirty" indicates if this copy of data is newer than the copy in the
  // original hdd file.
  // "vaddress_range_id" is the enclosing vaddress-range.
  // "virtual_page_address" is the virtual-page address.
  bool AddPage(void* data,
               uint64_t obj_size,
               bool is_dirty,
               V2HMapMetadata* v2hmap,
               uint32_t vaddress_range_id,
               void* virtual_page_address);

  // Load a flash page into memory.
  bool LoadPage(void* data,
                uint64_t obj_size,
                uint64_t flash_page_number,
                uint32_t vaddress_range_id,
                uint64_t vaddress_page_offset);

  // Get the F2V map item for a given flash page.
  F2VMapItem* GetItem(uint64_t page_number) { return &f2v_map_[page_number]; }

  // Evict objects and demote them to the next lower caching layer.
  // Return the number of objs that have been evicted.
  uint32_t EvictItems();

  void ShowStats();

 protected:
  // Backing flash-cache file.
  std::string flash_filename_;

  // File handle to the flash file.
  int flash_fd_;

  // Byte size of the flash-cache file.
  uint64_t flash_file_size_;

  // if this cache layer is ready.
  bool ready_;

  // Parent hmem instance to which this ram-cache belongs to.
  HybridMemory *hybrid_memory_;

  std::string name_;

  // Manages page allocation/free.
  PageAllocationTable  page_allocate_table_;

  // Manages page access history, which is used as part of
  // page eviction policy.
  PageStatsTable  page_stats_table_;

  // Array of F2VMapItem metadata. One entry for each flash page.
  F2VMapItem* f2v_map_;

  // Number of logical flash pages in this cache.
  uint64_t total_flash_pages_;

  // How many lookup are hits.
  uint64_t hits_count_;

  // How many pages have overflowed from this layer.
  uint64_t overflow_pages_;
};

#endif  // FLASH_CACHE_H_
