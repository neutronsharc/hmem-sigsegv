// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef HYBRID_MEMORY_H_
#define HYBRID_MEMORY_H_

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "asyncio_manager.h"
#include "asyncio_request.h"
#include "hybrid_memory_const.h"
#include "flash_cache.h"
#include "page_cache.h"
#include "ram_cache.h"
#include "sigsegv_handler.h"

// An instance of hybrid-memory.
class HybridMemory {
 public:
  HybridMemory() : ready_(false), asyncio_enabled_(false) {}

  virtual ~HybridMemory() {
    Release();
  }

  // Allocate internal resources, setup internal structs.
  bool Init(const std::string &ssd_filename,
            uint64_t page_buffer_size,
            uint64_t ram_buffer_size,
            uint64_t ssd_buffer_size,
            uint32_t hmem_intance_id);

  bool Release();

  void Lock();

  void Unlock();

  uint32_t instance_id() const { return hmem_instance_id_; }

  PageCache* GetPageCache() { return &page_cache_; }

  RAMCache* GetRAMCache() { return &ram_cache_; }

  FlashCache* GetFlashCache() { return &flash_cache_; }

  bool support_asyncio() const { return asyncio_enabled_; }

  AsyncIOManager* asyncio_manager() { return &asyncio_manager_; }

 protected:
  // Indicate if this hmem is ready.
  bool ready_;

  // Indicate if asyncio is successfully enabled.
  bool asyncio_enabled_;

  // Manager of async-io.
  AsyncIOManager asyncio_manager_;

  pthread_mutex_t lock_;

  std::string ssd_filename_;

  uint32_t hmem_instance_id_;

  uint64_t page_buffer_size_;
  uint64_t ram_buffer_size_;
  uint64_t ssd_buffer_size_;

  // L1 cache.
  PageCache page_cache_;

  // L2 cache.
  RAMCache ram_cache_;

  // L3 cache.
  FlashCache flash_cache_;
};

// One process can create only one HybridMemoryGroup, because all threads in
// this group share a same signal handler.
class HybridMemoryGroup {
 public:
  HybridMemoryGroup() : is_ready_(false) {}
  virtual ~HybridMemoryGroup();

  bool Init(const std::string &ssd_dirpath,
            const std::string &hmem_group_name,
            uint64_t page_buffer_size,
            uint64_t ram_buffer_size,
            uint64_t ssd_buffer_size,
            uint32_t number_hmem_instance);

  bool Release();

  // Given a offset-address (offset of the virtual-page from beginning
  // of vaddr-range), find what hmem-instance is responsible
  // to cache this address.
  HybridMemory* GetHybridMemory(uint64_t offset_address);

  HybridMemory* GetHybridMemoryFromInstanceId(uint32_t hmem_id) {
    return &hmem_instances_[hmem_id];
  }

 protected:
  bool is_ready_;

  // Als SSD files of this hmem-group are stored in this dir.
  std::string ssd_dirpath_;
  std::string hmem_group_name_;

  // Total resources used by this hmem group.
  uint64_t page_buffer_size_;
  uint64_t ram_buffer_size_;
  uint64_t ssd_buffer_size_;

  uint32_t number_hmem_instances_;
  HybridMemory hmem_instances_[MAX_HMEM_INSTANCES];
};

#endif  // HYBRID_MEMORY_H_
