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

  page_buffer_.Init(page_buffer_size >> PAGE_BITS);
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

bool HybridMemory::AddPageToPageBuffer(uint8_t* page,
                                       uint32_t size,
                                       uint32_t vaddr_range_id) {
  return page_buffer_.AddPage(page, size, vaddr_range_id);
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
  uint8_t* fault_page =
      (uint8_t*)(((uint64_t)fault_address) & ~((1ULL << 12) - 1));
  ucontext_t* context = (ucontext_t*)ucontext;

  // PC pointer value.
  unsigned char* pc = (unsigned char*)context->uc_mcontext.gregs[REG_RIP];

  // rwerror:  0: read, 2: write
  int rwerror = context->uc_mcontext.gregs[REG_ERR] & 2;
  if (number_page_faults % 100000 == 0) {
    dbg("Got SIGSEGV at address %p, page %p, pc %p, rw=%d\n",
        fault_address,
        fault_page,
        pc,
        rwerror);
  }

  if (!fault_address) {  // Invalid address, shall exit now.
    err("Invalid address=%p\n", fault_address);
    signal(SIGSEGV, SIG_DFL);
    err("now assert 0\n");
    assert(0);
  }

  // Find the parent vaddr_range this address belongs to.
  // If this fault-address doesn't belong to any vaddr_range, this fault
  // is caused by address outside of our control. Should fall back
  // to call default handler:  signal(SIGSEGV, SIG_DFL).
  VAddressRange* vaddr_range =
      vaddr_range_group.FindVAddressRange(fault_page);
  if (vaddr_range == NULL) {
    err("address=%p not within hybrid-memory range.\n", fault_address);
    signal(SIGSEGV, SIG_DFL);
    return;
  }
  HybridMemory* hmem =
      hmem_group.GetHybridMemory(fault_address - vaddr_range->GetAddress());
  hmem->Lock();

  uint64_t  prot_size = 4096;
  if (rwerror == 0) {  // A read fault.
    // TODO: find the target data from caching layer, copy target data into
    // this page (which requires set this page to PROT_WRITE),
    // then set it to PROT_READ.
    if (mprotect(fault_page, prot_size, PROT_READ) != 0) {
      err("in sigsegv: read mprotect %p failed...\n", fault_page);
      perror("mprotect error::  ");
      assert(0);
    }
  } else {  // A write fault.
    // TODO: set this page to PROT_WRITE,
    // find the target data from caching layer, copy target data into
    // this page.
    if (mprotect(fault_page, prot_size, PROT_WRITE) != 0) {
      err("in sigsegv: write mprotect %p failed...\n", fault_address);
      perror("mprotect error::  ");
      assert(0);
    }
  }

  // The "fault_page" has been materialized by OS.
  // Add this page to the list of materialized pages.
  // If this list exceeds limits, should madvise(DONTNEED) to release
  // some pages.
  hmem->AddPageToPageBuffer(
      fault_page, prot_size, vaddr_range->vaddr_range_id_);
  hmem->Unlock();
  return;
}

