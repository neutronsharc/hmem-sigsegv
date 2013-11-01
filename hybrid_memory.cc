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
#include "sigsegv_handler.h"
#include "vaddr_range.h"


static void SigSegvAction(int sig, siginfo_t* sig_info, void* ucontext);

bool HybridMemory::Init(const std::string &ssd_filename,
                        uint64_t page_buffer_size,
                        uint64_t ram_buffer_size,
                        uint64_t ssd_buffer_size,
                        uint32_t hmem_intance_id) {
  ssd_filename_ = ssd_filename;
  page_buffer_size_ = page_buffer_size;
  ram_buffer_size_ = ram_buffer_size;
  ssd_buffer_size_ = ssd_buffer_size;
  hmem_instance_id_ = hmem_intance_id;
  pthread_mutex_init(&lock_, NULL);
  // TODO: allocate buffers.

  char name[64];
  sprintf(name, "hmem-%d", hmem_intance_id);
  assert(page_cache_.Init(name, (page_buffer_size >> PAGE_BITS) + 1) ==
         true);
  return true;
}

bool HybridMemory::Release() {
  // TODO: release resources.
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
  // TODO: release resources.
}

HybridMemory* HybridMemoryGroup::GetHybridMemory(uint64_t offset_address) {
  uint64_t offset = (offset_address >> PAGE_BITS) >> VADDRESS_CHUNK_BITS;
  uint32_t hmem_id = offset % number_hmem_instances_;
  return &hmem_instances_[hmem_id];
}

static HybridMemoryGroup hmem_group;
static VAddressRangeGroup vaddr_range_group;
static SigSegvHandler sigsegv_handler;
// Number of page faults.
static uint64_t number_page_faults;

uint64_t GetNumberOfPageFaults() { return number_page_faults; }

bool InitHybridMemory(const std::string& ssd_dirpath,
                      const std::string& hmem_group_name,
                      uint64_t page_buffer_size,
                      uint64_t ram_buffer_size,
                      uint64_t ssd_buffer_size,
                      uint32_t number_hmem_instance) {
  if (vaddr_range_group.Init() == false) {
    err("vaddr range group error\n");
    return false;
  }
  if (hmem_group.Init(ssd_dirpath,
                      hmem_group_name,
                      page_buffer_size,
                      ram_buffer_size,
                      ssd_buffer_size,
                      number_hmem_instance) == false) {
    err("hmem group error\n");
    return false;
  }
  if (sigsegv_handler.InstallHandler(SigSegvAction) == false) {
    err("sigsegv handler error\n");
    return false;
  }
  return true;
}

void ReleaseHybridMemory() {
  sigsegv_handler.UninstallHandler();
  hmem_group.Release();
}

void *hmem_alloc(uint64_t size) {
  VAddressRange *vaddr_range = vaddr_range_group.AllocateVAddressRange(size);
  assert(vaddr_range != NULL);
  return vaddr_range->GetAddress();
}

void hmem_free(void *address) {
  VAddressRange *vaddr_range = vaddr_range_group.FindVAddressRange((uint8_t*)address);
  if (vaddr_range == NULL) {
    err("Address %p not exist in vaddr-range-group.\n", address);
    return;
  }
  vaddr_range_group.ReleaseVAddressRange(vaddr_range);
}

//void SigSegvHandler::SigSegvAction(int sig, siginfo_t* sig_info, void* ucontext) {
// It appears sigsegv_action shouldn't not be a class method.
static void SigSegvAction(int sig, siginfo_t* sig_info, void* ucontext) {
  ++number_page_faults;
  // Violating virtual address.
  uint8_t* fault_address = (uint8_t*)sig_info->si_addr;
  if (!fault_address) {  // Invalid address, shall exit now.
    err("Invalid address=%p\n", fault_address);
    // Restore default handler for sigsegv.
    signal(SIGSEGV, SIG_DFL);
    // Issue a signal.
    kill(getpid(), SIGSEGV);
    return;
  }

  uint8_t* fault_page = (uint8_t*)((uint64_t)fault_address & PAGE_MASK);
  ucontext_t* context = (ucontext_t*)ucontext;

  // PC pointer value.
  unsigned char* pc = (unsigned char*)context->uc_mcontext.gregs[REG_RIP];

  // rwerror:  0: read, 2: write
  int rwerror = context->uc_mcontext.gregs[REG_ERR] & 0x02;
  if (number_page_faults % 1000000 == 0) {
    dbg("Got SIGSEGV at address %p, page %p, pc %p, rw=%d\n",
        fault_address,
        fault_page,
        pc,
        rwerror);
  }

  // Find the parent vaddr_range this address belongs to.
  // If this fault-address doesn't belong to any vaddr_range, this fault
  // is caused by accessing address outside of hybrid-memory.
  // We should relay this signal to default handler.
  VAddressRange* vaddr_range =
      vaddr_range_group.FindVAddressRange(fault_page);
  if (vaddr_range == NULL) {
    err("address=%p not within hybrid-memory range.\n", fault_address);
    // Restore default handler for sigsegv.
    signal(SIGSEGV, SIG_DFL);
    // Issue a signal.
    kill(getpid(), SIGSEGV);
    return;
  }
  HybridMemory* hmem =
      hmem_group.GetHybridMemory(fault_address - vaddr_range->GetAddress());
  if (number_page_faults % 200000 == 0) {
    dbg("fault-addr %p, maps to hmem %d\n",
        fault_address,
        hmem->GetInstanceId());
  }
  hmem->Lock();
  // TODO:
  // 1. using page-offset (fault_page - vaddr_range_start) >> 12 to get
  //    index to virt-to-hybrid table to get metadata;
  V2HMapMetadata* v2hmap =
      vaddr_range->GetV2HMapMetadata(fault_address - vaddr_range->GetAddress());
  // 2. If v2h metadata shows the virt-address is in page-cache,
  //    this means another thread has just faulted on this page and populated
  //    this page. Nothing to do. return.
  if (v2hmap->exist_page_cache) {
    dbg("virt-address %p already in page cache\n", fault_address);
    hmem->Unlock();
    return;
  }
  // 3. If v2h metadata shows the virt-addr is in ram-cache, search
  //    ram cache, copy to virt-addr; mark exist in page-cache; return;
  //    has a copy in ram-buffer,
  // 4. If v2h shows the virt-addr is in flash-cache,  v2h also records
  //    in-flash offset,  load from flash and copy to virt-addr; mark exist
  //    in page-cache; return;
  // 5. Else,  if v2h shows it exists in hdd-file, the offset in vaddr-range
  //    is also file-offset. Load from file, mark exist in page-cache;
  //    return;
  uint64_t  prot_size = PAGE_SIZE;
  if (rwerror == 0) {  // A read fault.
    // NOTE: there is a vulnerability window here when we do the serial ops:
    //   Read-fault: (write-protect, populate the page, read-protect it), or,
    //   Writ-fault: (write-protect, populate the page).
    // After "write-protect" but before "populate the page", it's possible that
    // another thread access this same page without triggering page fault,
    // hence getting incorrect value.
    // There is nothing we can do to guard against this race.
    // It's the user's responsibility to enforce a higher level lock
    // to prevent race-access in a page.
    if (mprotect(fault_page, prot_size, PROT_WRITE) != 0) {
      err("in sigsegv: read mprotect %p failed...\n", fault_page);
      perror("mprotect error::  ");
      assert(0);
    }
    // TODO: find the target data from caching layer, copy target
    // data into this page.
    *fault_address = 0xff;
    if (mprotect(fault_page, prot_size, PROT_READ) != 0) {
      err("in sigsegv: read mprotect %p failed...\n", fault_page);
      perror("mprotect error::  ");
      assert(0);
    }
  } else {  // A write fault.
    if (mprotect(fault_page, prot_size, PROT_WRITE) != 0) {
      // TODO: find the target data from caching layer, copy target data into
      // this page.
      err("in sigsegv: write mprotect %p failed, page-faults=%ld\n",
          fault_address,
          number_page_faults);
      perror("mprotect error::  ");
      assert(0);
    }
  }
  // The "fault_page" has been materialized by OS.  We should add this page
  // to the list of materialized pages.
  // If this list exceeds limits, it will overflow to the next cache layer.
  hmem->GetPageCache()->AddPage(
      fault_page, prot_size, vaddr_range->vaddr_range_id_, v2hmap);
  hmem->Unlock();
  return;
}

