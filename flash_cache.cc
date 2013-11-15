#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "debug.h"
#include "flash_cache.h"
#include "hash_table.h"
#include "hybrid_memory.h"
#include "hybrid_memory_lib.h"
#include "hybrid_memory_const.h"
#include "ram_cache.h"
#include "page_cache.h"
#include "utils.h"
#include "vaddr_range.h"

bool FlashCache::Init(HybridMemory* hmem,
                      const std::string& name,
                      const std::string& flash_filename,
                      uint64_t max_flash_size) {
  assert(ready_ == false);
  uint64_t total_flash_pages = RoundUpToPageSize(max_flash_size) / PAGE_SIZE;

  uint64_t alignment = PAGE_SIZE;
  uint64_t map_byte_size = total_flash_pages * sizeof(F2VMapItem);
  // Init the F2V mapping table.
  assert(posix_memalign((void**)&f2v_map_, alignment, map_byte_size) == 0);
  assert(mlock(f2v_map_, map_byte_size) == 0);
  for (uint64_t i = 0; i < total_flash_pages; ++i) {
    f2v_map_[i].vaddress_range_id = INVALID_VADDRESS_RANGE_ID;
  }
  // Prepare the aux-buffer.
  aux_buffer_size_ = PAGE_SIZE << VADDRESS_CHUNK_BITS;
  assert(posix_memalign((void**)&aux_buffer_, alignment, aux_buffer_size_)
         == 0);
  assert(mlock(aux_buffer_, aux_buffer_size_) == 0);
  for (uint8_t* buf = aux_buffer_; buf < aux_buffer_ + aux_buffer_size_;
       buf += PAGE_SIZE) {
    aux_buffer_list_.push_back(buf);
  }
  // Init the page allocation table.
  assert(page_allocate_table_.Init(name + "-pg-alloc-table",
                                   total_flash_pages) == true);
  // Init the page access history table.
  assert(page_stats_table_.Init(name + "-pg-stats-table", total_flash_pages) ==
         true);
  // Open the flash-cache file.
  flash_fd_ =
      open(flash_filename.c_str(), O_CREAT | O_RDWR | O_TRUNC | O_DIRECT, 0666);
  if (flash_fd_ <= 0) {
    err("Unable to open flash file: %s\n", flash_filename.c_str());
    perror("Open file error.");
    assert(0);
  }
  flash_file_size_ = total_flash_pages * PAGE_SIZE;
  assert(ftruncate(flash_fd_, flash_file_size_) == 0);
  dbg("Has opened flash-cache file: %s, size = %ld, %ld flash-pages\n",
      flash_filename.c_str(), flash_file_size_, total_flash_pages);

  hybrid_memory_ = hmem;
  flash_filename_ = flash_filename;
  name_ = name;
  total_flash_pages_ = total_flash_pages;
  hits_count_ = 0;
  overflow_pages_ = 0;
  ready_ = true;
  return ready_;
}

void FlashCache:: Release() {
  if (ready_) {
    page_allocate_table_.Release();
    page_stats_table_.Release();
    close(flash_fd_);
    free(f2v_map_);
    free(aux_buffer_);
    aux_buffer_ = NULL;
    f2v_map_ = NULL;
    ready_ = false;
    hybrid_memory_ = NULL;
  }
}

uint32_t FlashCache::MigrateToHDD(
    std::vector<uint64_t>& flash_pages_writeto_hdd) {
  assert(flash_pages_writeto_hdd.size() > 0);
  // TODO: select the "best" version of the page to write to hdd.
  // From ram-cache? from flash-cache?
  // TODO: use libaio.
  for (uint64_t i = 0; i < flash_pages_writeto_hdd.size(); ++i) {
    uint64_t flash_page_number = flash_pages_writeto_hdd[i];
    F2VMapItem* f2vmap = &f2v_map_[flash_page_number];
    VAddressRange* vaddress_range =
        GetVAddressRangeFromId(f2vmap->vaddress_range_id);
    uint64_t vaddress_page_number = f2vmap->vaddress_page_offset;
    V2HMapMetadata* v2hmap =
        vaddress_range->GetV2HMapMetadata(vaddress_page_number << PAGE_BITS);
    void* virtual_page_address =
        vaddress_range->address() + (vaddress_page_number << PAGE_BITS);
    uint64_t hdd_file_offset =
        (vaddress_page_number << PAGE_BITS) + vaddress_range->hdd_file_offset();
    uint8_t* data_buffer;
    uint64_t io_size = PAGE_SIZE;
    if (flash_page_number == 12288) {
      dbg("%ld: flash-pg#: %ld, virt-page#: %ld at vaddr-range %d\n",
          i,
          flash_page_number,
          vaddress_page_number,
          f2vmap->vaddress_range_id);
    }
    if (v2hmap->dirty_page_cache) {
      // The virt-page has been materialized in page cache, and is being written
      // to. Don't write this page to hdd file because we don't know
      // if the update is complete.
      assert(v2hmap->exist_page_cache);
      dbg("flash page %ld: virt-page %p: exist in page-cache, but its "
          "flash-cache copy will be moved to hdd\n",
          i,
          virtual_page_address);
    } else if (v2hmap->dirty_ram_cache) {
      assert(v2hmap->exist_ram_cache);
      RAMCacheItem* ram_cache_item =
          hybrid_memory_->GetRAMCache()->GetItem(virtual_page_address);
      assert(ram_cache_item != NULL);
      assert(ram_cache_item->hash_key == virtual_page_address);
      assert(v2hmap == ram_cache_item->v2hmap);
      dbg("virt-page %p: exist in ram-cache, but its flash-cache copy "
          "will be moved to hdd\n",
          virtual_page_address);
#if 0
      data_buffer = ram_cache_item->data;
      assert(pwrite(vaddress_range->hdd_file_fd(),
                    data_buffer,
                    io_size,
                    hdd_file_offset) == io_size);
      hybrid_memory_->GetRAMCache()->Remove(ram_cache_item);
#endif
    } else if (v2hmap->dirty_flash_cache) {
      assert(v2hmap->exist_flash_cache);
      assert(aux_buffer_list_.size() > 0);
      data_buffer = aux_buffer_list_.back();
      aux_buffer_list_.pop_back();
      if (flash_page_number == 12288) {
        dbg("flash-page %ld: virt-page-number %ld: exist-dirty in flash-cache, "
            "moved to hdd\n",
            flash_page_number,
            vaddress_page_number);
      }
      // TODO: use async-io to issue multiple (read-flash, write-hdd) chains.
      assert(pread(flash_fd_,
                   data_buffer,
                   io_size,
                   flash_page_number << PAGE_BITS) == io_size);
      assert(pwrite(vaddress_range->hdd_file_fd(),
                    data_buffer,
                    io_size,
                    hdd_file_offset) == io_size);
      aux_buffer_list_.push_back(data_buffer);
      v2hmap->dirty_flash_cache = 0;
      v2hmap->exist_flash_cache = 0;
    }
  }
  return flash_pages_writeto_hdd.size();
}

uint32_t FlashCache::EvictItems(uint32_t pages_to_evict) {
  std::vector<uint64_t> pages;
  uint32_t evicted_pages =
      page_stats_table_.FindPagesWithMinCount(pages_to_evict, &pages);
  assert(evicted_pages == pages_to_evict);

  // If have backing hdd file, shall write dirty pages to back hdd.
  std::vector<uint64_t> flash_pages_writeto_hdd;
  for (uint32_t i = 0; i < evicted_pages; ++i) {
    uint64_t flash_page = pages[i];
    // This evicted pages must have be associated to an owner
    // vaddress-range.
    F2VMapItem* f2vmap = &f2v_map_[flash_page];
    if (f2vmap->vaddress_range_id >= INVALID_VADDRESS_RANGE_ID) {
      err("flash page %ld: its f2vmap->vaddr_rang_id is invalid: %d\n",
          flash_page, f2vmap->vaddress_range_id);
      assert(0);
    }
    VAddressRange* vaddress_range =
        GetVAddressRangeFromId(f2vmap->vaddress_range_id);
    uint64_t vaddress_page_number = f2vmap->vaddress_page_offset;
    V2HMapMetadata* v2hmap =
        vaddress_range->GetV2HMapMetadata(vaddress_page_number << PAGE_BITS);
    if (v2hmap->dirty_flash_cache && (vaddress_range->hdd_file_fd() > 0)) {
      flash_pages_writeto_hdd.push_back(flash_page);
    }
  }
  if (flash_pages_writeto_hdd.size() > 0) {
    MigrateToHDD(flash_pages_writeto_hdd);
  }

  for (uint32_t i = 0; i < evicted_pages; ++i) {
    uint64_t flash_page = pages[i];
    page_allocate_table_.FreePage(flash_page);
    F2VMapItem* f2vmap = &f2v_map_[flash_page];
    if (f2vmap->vaddress_range_id >= INVALID_VADDRESS_RANGE_ID) {
      err("flash page %ld: its f2vmap->vaddr_rang_id is invalid: %d\n",
          flash_page, f2vmap->vaddress_range_id);
      assert(0);
    }
    VAddressRange* vaddress_range =
        GetVAddressRangeFromId(f2vmap->vaddress_range_id);
    uint64_t vaddress_page_number = f2vmap->vaddress_page_offset;
    V2HMapMetadata* v2hmap =
        vaddress_range->GetV2HMapMetadata(vaddress_page_number << PAGE_BITS);
    v2hmap->dirty_flash_cache = 0;
    v2hmap->exist_flash_cache = 0;
    f2vmap->vaddress_range_id = INVALID_VADDRESS_RANGE_ID;
    f2vmap->vaddress_page_offset = 0;
  }
  return evicted_pages;
}

bool FlashCache::AddPage(void* page,
                         uint64_t obj_size,
                         bool is_dirty,
                         V2HMapMetadata* v2hmap,
                         uint32_t vaddress_range_id,
                         void* virtual_page_address) {
  assert(vaddress_range_id < INVALID_VADDRESS_RANGE_ID);
  assert(obj_size == PAGE_SIZE);
  uint64_t vaddress_page_offset =
      GetPageOffsetInVAddressRange(vaddress_range_id, virtual_page_address);
  // A flash-page number relative to the beginning of flash-cache file.
  uint64_t flash_page_number;
  if (v2hmap->exist_flash_cache) {
    assert(v2hmap->flash_page_offset < total_flash_pages_);
    flash_page_number = v2hmap->flash_page_offset;
    F2VMapItem* f2vmap = &f2v_map_[flash_page_number];
    assert(f2vmap->vaddress_page_offset == vaddress_page_offset);
    assert(f2vmap->vaddress_range_id == vaddress_range_id);
    if (flash_page_number == 12288) {
      dbg("^^^^^  pos 1: flash-page %ld for virt-page %ld, vaddr-range %d\n",
          flash_page_number, vaddress_page_offset, vaddress_range_id);
    }
  } else {
    if (page_allocate_table_.AllocateOnePage(&flash_page_number) == false) {
      // Evict some pages from flash-cache to make space.
      uint32_t pages_to_evict = 16;
      EvictItems(pages_to_evict);
      if (page_allocate_table_.AllocateOnePage(&flash_page_number) == false) {
        err("Unable to alloc flash page even after evict: virt-page %ld at "
            "vaddr-range-id %d\n",
            vaddress_page_offset,
            vaddress_range_id);
        assert(0);
      }
    }
    if (flash_page_number == 12288) {
      dbg("^^^^^  pos 2: flash-page %ld for virt-page %ld, vaddr-range %d\n",
          flash_page_number, vaddress_page_offset, vaddress_range_id);
    }
    assert(f2v_map_[flash_page_number].vaddress_range_id ==
           INVALID_VADDRESS_RANGE_ID);
  }
  if (!v2hmap->exist_flash_cache || is_dirty) {
    if (pwrite(flash_fd_, page, obj_size, flash_page_number << PAGE_BITS) !=
        obj_size) {
      err("Failed to write to flash-cache %s: flash-page: %ld, "
          "virtual-address=%p from vaddr-range %d\n",
          flash_filename_.c_str(),
          flash_page_number,
          virtual_page_address,
          vaddress_range_id);
      perror("flash pwrite failed: ");
      return false;
    }
  }

  f2v_map_[flash_page_number].vaddress_page_offset = vaddress_page_offset;
  f2v_map_[flash_page_number].vaddress_range_id = vaddress_range_id;

  V2HMapMetadata* v2h_map = GetV2HMap(vaddress_range_id, vaddress_page_offset);
  assert(v2hmap == v2h_map);
  v2hmap->exist_flash_cache = 1;
  v2hmap->dirty_flash_cache = is_dirty;
  v2hmap->flash_page_offset = flash_page_number;
  page_stats_table_.IncreaseAccessCount(flash_page_number, 1);
  if (flash_page_number == 12288) {
    dbg("flash page %ld: access-count=%ld, vaddress-range-id=%d\n",
        flash_page_number,
        page_stats_table_.AccessCount(flash_page_number),
        f2v_map_[flash_page_number].vaddress_range_id);
  }
  return true;
}

bool FlashCache::LoadPage(void* data,
                          uint64_t obj_size,
                          uint64_t flash_page_number,
                          uint32_t vaddress_range_id,
                          uint64_t vaddress_page_offset) {
  F2VMapItem* f2vmap = GetItem(flash_page_number);
  assert(f2vmap->vaddress_range_id == vaddress_range_id);
  assert(f2vmap->vaddress_page_offset == vaddress_page_offset);
  assert((uint64_t)data % 512 == 0);
  assert(obj_size == PAGE_SIZE);

  if (pread(flash_fd_, data, obj_size, flash_page_number << PAGE_BITS) !=
      obj_size) {
    err("Failed to read flash-cache %s: flash-page %ld, to vaddr-range %d, "
        "page %ld\n",
        flash_filename_.c_str(),
        flash_page_number,
        vaddress_range_id,
        vaddress_page_offset);
    perror("flash pread failed: ");
    return false;
  }
  page_stats_table_.IncreaseAccessCount(flash_page_number, 1);
  return true;
}

bool FlashCache::LoadFromHDDFile(VAddressRange* vaddr_range,
                                 void* page,
                                 V2HMapMetadata* v2hmap,
                                 bool read_ahead) {
  uint64_t hdd_file_offset = (uint64_t)page - (uint64_t)vaddr_range->address() +
                             vaddr_range->hdd_file_offset();
  uint64_t read_size = PAGE_SIZE;
  if (!read_ahead) {
    if (pread(vaddr_range->hdd_file_fd(), page, read_size, hdd_file_offset) !=
        read_size) {
      err("Failed to read hdd-file at flash-cache %s: vaddr-range %d, "
          "page %p\n",
          name_.c_str(),
          vaddr_range->vaddress_range_id(),
          page);
      perror("flash-cache read hdd-file failed: ");
      return false;
    }
  } else {
    read_size = PAGE_SIZE << VADDRESS_CHUNK_BITS;
    uint64_t virtual_chunk = (uint64_t)page & ~((1ULL << (PAGE_BITS + VADDRESS_CHUNK_BITS)) - 1);
    hdd_file_offset = virtual_chunk - (uint64_t)vaddr_range->address() +
                      vaddr_range->hdd_file_offset();
    // TODO: at read-ahead mode, read an entire chunk from hdd file,
    // and save these pages to flash.
  }
  return true;
}

void FlashCache::ShowStats() {
  printf(
      "\n\n*****\tflash-cache: %s, flash-file: %s, total-flash pages %ld, "
      "used-flash-pages %ld, available flash pages %ld\n",
      name_.c_str(),
      flash_filename_.c_str(),
      total_flash_pages_,
      page_allocate_table_.used_pages(),
      page_allocate_table_.free_pages());
}
