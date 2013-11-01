#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "avl.h"
#include "hybrid_memory_const.h"
#include "vaddr_range.h"
#include "page_cache.h"

bool PageCache::Init(const std::string& name, uint32_t max_allowed_pages) {
  assert(max_allowed_pages > 0);
  max_allowed_pages_ = max_allowed_pages;
  bool pin_memory = true;
  assert(item_list_.Init(
             name + "-pagecache-itemlist", max_allowed_pages, 0, pin_memory) ==
         true);
  name_ = name;
  return true;
}

bool PageCache::Release() {
  if (queue_.size() > 0) {
    // TODO: release pages still in the queue.
    dbg("Has %ld pages in page-cache\n", queue_.size());
  }
  item_list_.Release();
  return true;
}

bool PageCache::AddPage(uint8_t* page,
                        uint32_t size,
                        uint32_t vaddr_range_id,
                        V2HMapMetadata* v2hmap) {
  PageCacheItem* item = item_list_.New();
  if (item == NULL) {
    for (uint32_t i = 0; i < 10; ++i) {
      PageCacheItem* olditem = (PageCacheItem*)queue_.front();
      queue_.pop();
      assert(olditem != NULL);
      // TODO:
      // If the olditem contains dirty data, should flush this data to next
      // layer of cache.
      // There is a race-condition here:
      // Thread 1 unproected page1 and is in the middle of writing to it.
      // Thread 2 triggers a sigsegv and decides to release page1. Thread2 will
      // copy page1 to next cache layer since page1 has "dirty" flag set.
      // As a result, an incomplete copy of page1 is moved to next cache layer
      // while thread 1 is still writing to this page.
      //
      // The only thing we can do at sig handler to defend this race is to
      // read-protect the page before copying, so thread1 will see a sigsegv
      // when writing to it and fall back sig handler to load it from
      // the lower cache layer.
      //   if olditem->dirty {
      //     mprotect(olditem, READ);
      //     copy olditem to next layer of cache;
      //   }
      //   mprotect(olditem, NONE);
      //   madvise(olditem, dontneed);
      olditem->v2hmap->exist_page_cache = 0;
      assert(madvise(olditem->page, olditem->size, MADV_DONTNEED) == 0);
      assert(mprotect(olditem->page, olditem->size, PROT_NONE) == 0);
      item_list_.Free(olditem);
    }
    item = item_list_.New();
  }
  assert(item != NULL);
  item->page = page;
  item->size = size;
  item->vaddr_range_id = vaddr_range_id;
  item->v2hmap = v2hmap;
  queue_.push(item);
  return true;
}
