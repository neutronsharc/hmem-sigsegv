#ifndef HYBRID_MEMORY_H_
#define HYBRID_MEMORY_H_

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "hybrid_memory_const.h"
#include "page_buffer.h"

class PageBuffer;

// An instance of hybrid-memory.
class HybridMemory {
 public:
  HybridMemory() {}
  virtual ~HybridMemory() {}

  // Allocate internal resources, setup internal structs.
  bool Init(const std::string &ssd_filename,
            uint64_t page_buffer_size,
            uint64_t ram_buffer_size,
            uint64_t ssd_buffer_size,
            uint32_t hmem_intance_id);

  bool Release();

  void Lock();

  void Unlock();

  bool AddPageToPageBuffer(uint8_t* page,
                           uint32_t size,
                           uint32_t vaddr_range_id);

  uint32_t GetInstanceId() const { return hmem_instance_id_; }

 protected:
  pthread_mutex_t lock_;

  std::string ssd_filename_;

  uint32_t hmem_instance_id_;

  uint64_t page_buffer_size_;
  uint64_t ram_buffer_size_;
  uint64_t ssd_buffer_size_;

  PageBuffer page_buffer_;
};

// One process can create only one HybridMemoryGroup, because all threads in
// this group share a same signal handler.
class HybridMemoryGroup {
 public:
  HybridMemoryGroup() {}
  virtual ~HybridMemoryGroup();

  bool Init(const std::string &ssd_dirpath,
            const std::string &hmem_group_name,
            uint64_t page_buffer_size,
            uint64_t ram_buffer_size,
            uint64_t ssd_buffer_size,
            uint32_t number_hmem_instance);

  bool Release();

  // Given a offset-address (offset of the virtual-address from beginning
  // of vaddr-range), find what hmem-instance is responsible
  // to cache this address.
  HybridMemory* GetHybridMemory(uint64_t offset_address);

 protected:
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

/////////////////////////////////////////////////////////
// Global functions to init/exit hybrid memory.
bool InitHybridMemory(const std::string& ssd_dirpath,
                      const std::string& hmem_group_name,
                      uint64_t page_buffer_size,
                      uint64_t ram_buffer_size,
                      uint64_t ssd_buffer_size,
                      uint32_t number_hmem_instance);

void ReleaseHybridMemory();

void *hmem_alloc(uint64_t size);

void hmem_free(void *address);

uint64_t GetNumberOfPageFaults();

#endif  // HYBRID_MEMORY_H_
