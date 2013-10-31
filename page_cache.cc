#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <bitset>

#include "avl.h"
#include "hybrid_memory_const.h"
#include "vaddr_range.h"
#include "page_cache.h"


bool PageCache::Init(uint32_t max_allowed_pages) {
  assert(max_allowed_pages > 0);
  max_allowed_pages_ = max_allowed_pages;
  item_list_ = new FreeList<PageCacheItem>(max_allowed_pages);
  assert(item_list_ != NULL);
  return true;
}

bool PageCache::Release() {
  if (queue_.size() > 0) {
    dbg("Has %ld pages in page-cache\n", queue_.size());
  }
  return true;
}

bool PageCache::AddPage(uint8_t* page,
                        uint32_t size,
                        uint32_t vaddr_range_id,
                        V2HMapMetadata* v2hmap) {
  PageCacheItem* item = item_list_->New();
  if (item == NULL) {
    for (uint32_t i = 0; i < 10; ++i) {
      PageCacheItem* olditem = (PageCacheItem*)queue_.front();
      queue_.pop();
      assert(olditem != NULL);
      // Release materialized pages, overflow to the next layer of cache.
      olditem->v2hmap->exist_page_cache = 0;
      assert(madvise(olditem->page, olditem->size, MADV_DONTNEED) == 0);
      assert(mprotect(olditem->page, olditem->size, PROT_NONE) == 0);
      item_list_->Free(olditem);
    }
    item = item_list_->New();
  }
  assert(item != NULL);
  item->page = page;
  item->size = size;
  item->vaddr_range_id = vaddr_range_id;
  item->v2hmap = v2hmap;
  queue_.push(item);
  return true;
}
