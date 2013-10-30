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
#include "page_buffer.h"


bool PageBuffer::Init(uint32_t max_allowed_pages) {
  assert(max_allowed_pages > 0);
  max_allowed_pages_ = max_allowed_pages;
  item_list_ = new FreeList<PageBufferItem>(max_allowed_pages);
  assert(item_list_ != NULL);
  return true;
}

bool PageBuffer::Release() {
  return true;
}

bool PageBuffer::AddPage(uint8_t *page, uint32_t size, uint32_t vaddr_range_id) {
  PageBufferItem* item = item_list_->New();
  if (item == NULL) {
    for (uint32_t i = 0; i < 10; ++i) {
      PageBufferItem* olditem = (PageBufferItem*)queue_.front();
      queue_.pop();
      assert(olditem != NULL);
      // TODO: release materialized pages, overflow the next layer of cache.
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
  queue_.push(item);
  return true;
}
