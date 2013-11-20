// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

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
  bool page_align = true;
  bool pin_memory = true;
  assert(free_list_.Init(name + "-freelist", number_pages, object_datasize,
                         page_align, pin_memory) == true);

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

RAMCacheItem* RAMCache::GetItem(void* virtual_address) {
  assert(((uint64_t)virtual_address & (PAGE_SIZE - 1)) == 0);
  RAMCacheItem* item =
      hash_table_.Lookup((void*)virtual_address, sizeof(void*));
  if (item) {
    assert(item->hash_key == virtual_address);
  }
  // This item gets an recent access. Move it to front of LRU list.
  lru_list_.Update(item);
  return item;
}

uint32_t RAMCache::EvictItems() {
  uint32_t items_to_evict = 16;
  RAMCacheItem* items[items_to_evict];
  // Scan backwards from Least-Recent-Used objs.
  RAMCacheItem* item = lru_list_.tail();
  uint32_t items_found = 0;
  while (item && (items_found < items_to_evict)) {
    V2HMapMetadata* v2hmap = item->v2hmap;
    assert(v2hmap->exist_ram_cache);

    if (v2hmap->exist_page_cache == 0) {
      items[items_found++] = item;
    }
    item = item->lru_prev;
  }
  for (uint32_t i = 0; i < items_found; ++i) {
    item = items[i];
    V2HMapMetadata* v2hmap = item->v2hmap;
    if (!v2hmap->exist_flash_cache || v2hmap->dirty_ram_cache) {
      // Move these objs to the next lower cache layer.
      hybrid_memory_->GetFlashCache()->AddPage(item->data,
                                               PAGE_SIZE,
                                               v2hmap->dirty_ram_cache,
                                               v2hmap,
                                               item->vaddress_range_id,
                                               item->hash_key);
    }
    lru_list_.Unlink(item);
    hash_table_.Remove(item->hash_key, sizeof(void*));

    v2hmap->exist_ram_cache = 0;
    v2hmap->dirty_ram_cache = 0;
    item->v2hmap = NULL;
    item->hash_key = NULL;
    free_list_.Free(item);
  }
  return items_found;
}

void RAMCache::Remove(RAMCacheItem* ram_cache_item) {
  lru_list_.Unlink(ram_cache_item);
  hash_table_.Remove(ram_cache_item->hash_key, sizeof(void*));
  V2HMapMetadata* v2hmap = ram_cache_item->v2hmap;
  v2hmap->exist_ram_cache = 0;
  v2hmap->dirty_ram_cache = 0;
  ram_cache_item->v2hmap = NULL;
  ram_cache_item->hash_key = NULL;
  free_list_.Free(ram_cache_item);
}

bool RAMCache::AddPage(void* page,
                       uint64_t obj_size,
                       bool is_dirty,
                       V2HMapMetadata* v2hmap,
                       uint32_t vaddress_range_id) {
  assert(((uint64_t)page & (PAGE_SIZE - 1)) == 0);
  assert(obj_size <= PAGE_SIZE);

  RAMCacheItem* item = hash_table_.Lookup(page, sizeof(void*));
  if (item) {
    assert(item->v2hmap == v2hmap);
    assert(item->hash_key == page);
    assert(v2hmap->exist_ram_cache == 1);
    // The virt-page already has a cached copy in current layer.
    if (is_dirty) {
      memcpy(item->data, page, obj_size);
      item->v2hmap->dirty_ram_cache = 1;
    }
    lru_list_.Update(item);
  } else {
    // The virt-page hasn't been cahched in this layer.
    item = free_list_.New();
    while (!item) {
      // Traverse LRU-list to evict some old items to the next layer.
      if (EvictItems() == 0) {
        err("Unable to evict objs from RAM-Cache!\n");
        return false;
      }
      item = free_list_.New();
    }
    assert(item != NULL);
    memcpy(item->data, page, obj_size);
    item->hash_key = page;
    item->vaddress_range_id = vaddress_range_id;
    item->v2hmap = v2hmap;
    item->v2hmap->exist_ram_cache = 1;
    item->v2hmap->dirty_ram_cache = is_dirty;

    // Insert the newly cached obj to hash-table.
    hash_table_.Insert(item, sizeof(void*));
    // Link it to LRU list.
    lru_list_.Link(item);
  }
  return true;
}
