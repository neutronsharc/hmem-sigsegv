// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef PAGE_CACHE_H_
#define PAGE_CACHE_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue>
#include "free_list.h"

struct V2HMapMetadata;
struct HybridMemory;

// Each materialized page is represented by a page-buffer-item.
struct PageCacheItem {
  // The virt-address page which is materialized.
  void* page;

  // Size of the data in virt-page. At present it's always page size.
  uint32_t size;

  // Enclosing vaddr_range of this page.
  uint32_t vaddr_range_id;

  union {
    // v2h map metadata for this virt-page.
    V2HMapMetadata* v2hmap;
    // The free-list wants each obj with a field to hold payload.
    // This is to satisfy the free-list.
    void * data;
  };
} __attribute__((__packed__));

// Page cache is the 1st layer of cache that stores all
// materialized pages (virtual pages that have physical pages allocated by OS).
class PageCache {
 public:
  PageCache() : ready_(false), hybrid_memory_(NULL) {}
  virtual ~PageCache() { Release(); }

  bool Init(HybridMemory* hmem,
            const std::string& name,
            uint64_t max_cache_size);

  bool Release();

  // Add a new pae to this layer of caching.
  bool AddPage(void* page,
               uint32_t size,
               bool is_dirty,
               V2HMapMetadata* v2hmap,
               uint32_t vaddr_range_id);

  // Evict to the next caching layer beneath this layer.
  uint32_t EvictItems();

  const std::string& name() const { return name_; }

 protected:
  bool ready_;

  HybridMemory* hybrid_memory_;

  // The queue of materialized pages in the page buffer.
  std::queue<PageCacheItem*> queue_;

  // A free-list of page-buffer-item metadata objs.
  FreeList<PageCacheItem> item_list_;

  // Allow up to this many materialized pages in the queue.
  uint64_t max_cache_size_;

  std::string name_;
};

#endif  // PAGE_CACHE_H_
