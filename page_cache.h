#ifndef PAGE_CACHE_H_
#define PAGE_CACHE_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue>
#include "free_list.h"

struct V2HMapMetadata;

// Each materialized page is represented by a page-buffer-item.
struct PageCacheItem {
  // The materialized page.
  uint8_t* page;

  // Size of the page.
  uint32_t size;

  // Enclosing vaddr_range of this page.
  uint32_t vaddr_range_id;  // From parent hmem-instance-id.

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
  PageCache() {}
  virtual ~PageCache() { Release(); }

  bool Init(const std::string& name, uint32_t max_allowed_pages);
  bool Release();

  bool AddPage(uint8_t* page,
               uint32_t size,
               uint32_t vaddr_range_id,
               V2HMapMetadata* v2hmap);

  const std::string& name() const { return name_; }

 protected:
  uint32_t OverflowToNextLayer();

  // The queue of materialized pages in the page buffer.
  std::queue<PageCacheItem*> queue_;

  // A free-list of page-buffer-item metadata objs.
  FreeList<PageCacheItem> item_list_;

  // Allow up to this many materialized pages in the queue.
  uint32_t max_allowed_pages_;

  std::string name_;
};

#endif  // PAGE_CACHE_H_
