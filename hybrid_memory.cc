#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include "debug.h"
#include "hybrid_memory.h"
#include "page_cache.h"
#include "ram_cache.h"
#include "sigsegv_handler.h"
#include "vaddr_range.h"

bool HybridMemory::Init(const std::string &ssd_dirpath,
                        uint64_t page_buffer_size,
                        uint64_t ram_buffer_size,
                        uint64_t ssd_buffer_size,
                        uint32_t hmem_intance_id) {
  assert(ready_ == false);
  page_buffer_size_ = page_buffer_size;
  ram_buffer_size_ = ram_buffer_size;
  ssd_buffer_size_ = ssd_buffer_size;
  hmem_instance_id_ = hmem_intance_id;
  pthread_mutex_init(&lock_, NULL);

  // TODO: Prepare cache layers.

  char name[64];
  sprintf(name, "hmem-%d", hmem_intance_id);
  std::string strname = name;
  std::string flash_filename = ssd_dirpath + "flashcache-" + strname;
  assert(page_cache_.Init(this, strname + "-pagecache", page_buffer_size) ==
         true);
  assert(ram_cache_.Init(this, strname + "-ramcache", ram_buffer_size) == true);
  assert(flash_cache_.Init(
      this, strname + "-flashcache", flash_filename, ssd_buffer_size) == true);
  ready_ = true;
  return ready_;
}

bool HybridMemory::Release() {
  // TODO: release resources.
  if (ready_) {
    page_cache_.Release();
    ram_cache_.Release();
    flash_cache_.Release();
    ready_ = false;
  }
  return true;
}

void HybridMemory::Lock() {
  pthread_mutex_lock(&lock_);
}

void HybridMemory::Unlock() {
  pthread_mutex_unlock(&lock_);
}

HybridMemoryGroup::~HybridMemoryGroup() {
  // TODO: release resources.
}

bool HybridMemoryGroup::Init(const std::string& ssd_dirpath,
                             const std::string& hmem_group_name,
                             uint64_t page_buffer_size,
                             uint64_t ram_buffer_size,
                             uint64_t ssd_buffer_size,
                             uint32_t number_hmem_instances) {
  assert(number_hmem_instances <= MAX_HMEM_INSTANCES);
  ssd_dirpath_ = ssd_dirpath;
  page_buffer_size_ = page_buffer_size;
  ram_buffer_size_ = ram_buffer_size;
  ssd_buffer_size_ = ssd_buffer_size;
  number_hmem_instances_ = number_hmem_instances;

  for (uint32_t i = 0; i < number_hmem_instances_; ++i) {
    if (!hmem_instances_[i].Init(ssd_dirpath,
                                 page_buffer_size / number_hmem_instances,
                                 ram_buffer_size / number_hmem_instances,
                                 ssd_buffer_size / number_hmem_instances,
                                 i)) {
      err("hmem instance %d failed to init.\n", i);
      return false;
    }
  }
  return true;
}

bool HybridMemoryGroup::Release() {
  for (uint32_t i = 0; i < number_hmem_instances_; ++i) {
    hmem_instances_[i].Release();
  }
}

HybridMemory* HybridMemoryGroup::GetHybridMemory(uint64_t offset_address) {
  uint64_t offset = (offset_address >> PAGE_BITS) >> VADDRESS_CHUNK_BITS;
  uint32_t hmem_id = offset % number_hmem_instances_;
  return &hmem_instances_[hmem_id];
}
