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

struct RAMCacheItem;
struct V2HMapMetadata;
class HybridMemory;

// Flash-to-virtual-address mapping metadata for each flash-page.
struct F2VMapItem {
  // Hosting vaddr-range id of the virtual-page cached in this flash-page.
  uint32_t vaddress_range_id : 8;
  // The cached virtual-pages's offset in page unit in the hosting vaddr-range.
  uint32_t page_offset : 24;
} __attribute__((__packed__));

// Flash cache is the 3rd layer of cache that stores all
// pages that overflows from 2st layer (RAM-cache).
class FlashCache {
 public:
  FlashCache() : ready_(false), hybrid_memory_(NULL) {}
  virtual ~FlashCache() { Release(); }

  bool Init(HybridMemory* hmem,
            const std::string& name,
            uint64_t number_flash_pages);

  bool Release();

  bool AddPage(void* page, uint64_t obj_size, V2HMapMetadata* v2hmap);

  uint64_t TotalObjects() { return free_list_.TotalObjects(); }

  uint64_t NumberOfFreeObjects() { return free_list_.AvailObjects(); }

  uint64_t CachedObjects() {
    return free_list_.TotalObjects() - free_list_.AvailObjects();
  }

  // Lookup the hash table to find an item whose key equals to
  // with the virtual-address. This item caches a page-copy of the
  // virtual-address.
  RAMCacheItem* GetItem(void *virtual_address);

  // Cache the given page.
  // "page" is the virt-address that has been materialized and contains
  // "obj_size" of data.
  // "is_dirty" indicates if this cached copy contains update that hasn't
  // made to the original location in backing storage (usually hdd file).
  bool AddPage(void* page,
               uint64_t data_size,
               V2HMapMetadata* v2hmap,
               bool is_dirty);

  // Evict objects and demote them to the next lower caching layer.
  // Return the number of objs that have been evicted.
  uint32_t EvictItems();

 protected:
  // if this cache layer is ready.
  bool ready_;

  // Parent hmem instance to which this ram-cache belongs to.
  HybridMemory *hybrid_memory_;

  std::string name_;

  // Manages page allocation/free.
  PageAllocationTable  page_allocate_table_;

  // Number of logical flash pages in this cache.
  uint64_t number_flash_pages_;

  uint64_t hits_count_;

  // How many pages have overflowed from this layer.
  uint64_t overflow_pages_;
};

#endif  // FLASH_CACHE_H_
