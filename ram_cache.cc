#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "hybrid_memory.h"
#include "hybrid_memory_const.h"
#include "vaddr_range.h"
#include "page_cache.h"
#include "hash_table.h"
#include "ram_cache.h"
#include "utils.h"

bool RAMCache::Init(HybridMemory* hmem,
                    const std::string& name,
                    uint64_t max_cache_size) {
  assert(ready_ == false);
  max_cache_size_ = RoundUpToPageSize(max_cache_size);
  assert(max_cache_size_ > 0);

  // Prepare the free-list.
  uint64_t number_pages = max_cache_size_ >> PAGE_BITS;
  uint64_t object_datasize = PAGE_SIZE;
  bool pin_memory = true;
  assert(free_list_.Init(
             name + "-freelist", number_pages, object_datasize, pin_memory) ==
         true);

  // Prepare hash table. Load-factor = 4/3.
  uint64_t hash_buckets = number_pages * 3 / 4;
  assert(hash_table_.Init(name + "-hashtable", hash_buckets, pin_memory) ==
         true);

  hybrid_memory_ = hmem;
  name_ = name;
  ready_ = true;
  hits_count_ = 0;
  miss_count_ = 0;
  return ready_;
}

bool RAMCache::Release() {
  if (ready_) {
    free_list_.Release();
    hash_table_.Release();
    ready_ = false;
  }
}

RAMCacheItem* RAMCache::GetItem(void *virtual_address) {
  assert((uint64_t)virtual_address & (PAGE_SIZE - 1) == 0);
  RAMCacheItem* item =
      hash_table_.Lookup((void*)virtual_address, sizeof(void*));
  if (item) {
    assert(item->hash_key == virtual_address);
  }
  return item;
}

bool RAMCache::AddPage(void* page,
                       uint64_t obj_size,
                       V2HMapMetadata* v2hmap) {
  assert((uint64_t)page & (PAGE_SIZE - 1) == 0);
  assert(obj_size <= PAGE_SIZE);

  RAMCacheItem* item = hash_table_.Lookup(page, sizeof(void*));
  if (item) {
    // The virt-page already has a cached copy in current layer.
    assert(item->hash_key == page);
    assert(item->v2hmap == v2hmap);
    if (v2hmap->dirty_page_cache) {
      memcpy(item->data, page, obj_size);
      item->v2hmap->dirty_ram_cache = 1;
    }
    lru_list_.Update(item);
    item->v2hmap->exist_ram_cache = 1;
  } else {
    // The virt-page hasn't been cahched in this layer.
    item = free_list_.New();
    while (!item) {
      // TODO: traverse LRU-list to evict some old items to the next layer.
      err("RAMCache full! Need to evict some objs.\n");
    }
    assert(item != NULL);
    memcpy(item->data, page, obj_size);
    item->hash_key = page;
    item->v2hmap = v2hmap;
    // Insert the newly cached obj to hash-table.
    hash_table_.Insert(item, sizeof(void*));
    // Link it to LRU list.
    lru_list_.Link(item);
    item->v2hmap->exist_ram_cache = 1;
    item->v2hmap->dirty_ram_cache = v2hmap->dirty_page_cache;
  }
  return true;
}
