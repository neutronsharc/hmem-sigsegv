#ifndef FLASH_CACHE_H_
#define FLASH_CACHE_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue>
#include "hash_table.h"
#include "lru_list.h"
#include "free_list.h"

struct RAMCacheItem;
struct V2HMapMetadata;
class HybridMemory;

// Metadata for each object cached in this layer.
// Currently each object represents a virtual-page.
struct FlashCacheItem {
  // pointers to link this item to LRU list.
  RAMCacheItem *lru_prev;
  RAMCacheItem *lru_next;

  // Fields for hash-table.
  // This is the virt-address that this ram-cache-data is associated to.
  void *hash_key;
  RAMCacheItem* hash_next;

  // Back pointer to the V2H metadata.
  V2HMapMetadata* v2hmap;

  // Real memory where data of the virt-page is cached.
  // This memory should be pinned, and page-aligned to
  // facilitate direct IO to the next cache layer (flash or hdd).
  void *data;
  // TODO: As of now "data" ram-cache only caches page unit.
  // In the future ram-cache may cache objs of arbitrary size.
  // At that time we may add a field "obj_size".
} __attribute__((__packed__));

// Flash cache is the 3nd layer of cache in hybrid-memory arch.
// It stores data that couldn't fit in the ram-cache.
class FlashCache {
 public:
  RAMCache() : ready_(false), hybrid_memory_(NULL) {}
  virtual ~RAMCache() { Release(); }

  bool Init(HybridMemory* hmem,
            const std::string& name,
            uint64_t max_cache_size);

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

  // The lru list that sorts all cached objs.
  LRUList<RAMCacheItem> lru_list_;

  // A hash table to lookup a cached item given a virtual-address.
  HashTable<RAMCacheItem> hash_table_;

  // All cache-items come from this free list.
  // Each item has an embedded "data" field.
  FreeList<RAMCacheItem> free_list_;

  // Allow up to this many materialized pages in the queue.
  uint64_t max_cache_size_;

  uint64_t hits_count_;

  uint64_t miss_count_;
};

#endif  // PAGE_CACHE_H_