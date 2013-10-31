#ifndef RAM_CACHE_H_
#define RAM_CACHE_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue>
#include "free_list.h"

// Each materialized page is represented by a page-buffer-item.
typedef struct PageCacheItem_s {
  // The materialized page.
  uint8_t  *page;
  // Size of the page.
  uint32_t size;
  // Enclosing vaddr_range of this page.
  uint32_t vaddr_range_id;  // From parent hmem-instance-id.
} PageCacheItem;

// Page cache is the 1st layer of cache that stores all
// materialized pages (virtual pages that have physical pages allocated by OS).
class RAMCache {
 public:
  RAMCache() {}
  virtual ~RAMCache() { Release(); }

  bool Init(uint32_t max_cached_pages);
  bool Release();

  bool AddPage(uint8_t *page, uint32_t size, uint32_t vaddr_range_id);

 protected:
  uint32_t OverflowToNextLayer();

  // The queue of materialized pages in the page buffer.
  std::queue<PageCacheItem*> queue_;

  // A free-list of page-buffer-item metadata objs.
  FreeList<PageCacheItem>* item_list_;

  // Allow up to this many materialized pages in the queue.
  uint32_t max_allowed_pages_;
};

#endif  // PAGE_CACHE_H_
