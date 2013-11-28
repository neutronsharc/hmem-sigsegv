// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

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
#include "hybrid_memory_lib.h"
#include "sigsegv_handler.h"
#include "vaddr_range.h"

static HybridMemoryGroup hmem_group;

static VAddressRangeGroup vaddr_range_group;

static SigSegvHandler sigsegv_handler;

static void SigSegvAction(int sig, siginfo_t* sig_info, void* ucontext);

static uint64_t number_page_faults;

static uint64_t hit_hdd_file = 0;

static uint64_t hit_flash_cache = 0;

static uint64_t hit_ram_cache = 0;

// How many pages were not found in hybrid-memory.
static uint64_t unfound_pages;

// How many pages were found in hybrid-memory.
static uint64_t found_pages;

uint64_t NumberOfPageFaults() { return number_page_faults; }

uint64_t FoundPages() { return found_pages; }

uint64_t UnFoundPages() { return unfound_pages; }

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

void* hmem_map(const std::string& hdd_filename,
               uint64_t size,
               uint64_t hdd_file_offset) {
  VAddressRange* vaddr_range = vaddr_range_group.AllocateVAddressRange(
      size, hdd_filename, hdd_file_offset);
  assert(vaddr_range != NULL);
  return vaddr_range->address();
}

void* hmem_alloc(uint64_t size) {
  VAddressRange *vaddr_range = vaddr_range_group.AllocateVAddressRange(size);
  assert(vaddr_range != NULL);
  return vaddr_range->address();
}

void hmem_free(void* address) {
  VAddressRange* vaddr_range =
      vaddr_range_group.FindVAddressRange((uint8_t*)address);
  if (vaddr_range == NULL) {
    err("Address %p not exist in vaddr-range-group.\n", address);
    return;
  }
  vaddr_range_group.ReleaseVAddressRange(vaddr_range);
}

uint64_t GetPageOffsetInVAddressRange(uint32_t vaddress_range_id, void* page) {
  return vaddr_range_group.VAddressRangeFromId(vaddress_range_id)
      ->GetPageOffset(page);
}

V2HMapMetadata* GetV2HMap(uint32_t vaddress_range_id, uint64_t page_offset) {
  return vaddr_range_group.VAddressRangeFromId(vaddress_range_id)
      ->GetV2HMapMetadata(page_offset << PAGE_BITS);
}

VAddressRange* GetVAddressRangeFromId(uint32_t vaddress_range_id) {
  return vaddr_range_group.VAddressRangeFromId(vaddress_range_id);
}

void HybridMemoryStats() {
   printf("\n=================");
   printf(
       "hybrid-memory: hit-ram-cache=%ld, hit-flash-cache=%ld, found-pages = "
       "%ld, unfound-pages=%ld",
       hit_ram_cache,
       hit_flash_cache,
       found_pages,
       unfound_pages);
   hmem_group.GetHybridMemoryFromInstanceId(0)->GetFlashCache()->ShowStats();
}

// Search hybrid-memory's internal cache layers to find
// a copy of the cache corresponding to the virt-address "falut_page".
static bool LoadDataFromHybridMemory(void* fault_page,
                                     VAddressRange* vaddr_range,
                                     HybridMemory* hmem,
                                     V2HMapMetadata* v2hmap) {
  assert(!v2hmap->exist_page_cache);
  if (v2hmap->exist_ram_cache) {
    RAMCacheItem* ram_cache_item = hmem->GetRAMCache()->GetItem(fault_page);
    if (!ram_cache_item) {
      err("v2hmap shows address %p exists in ram-cache, but cannot find.\n",
          fault_page);
      _exit(0);
    }
    memcpy(fault_page, ram_cache_item->data, PAGE_SIZE);
    ++hit_ram_cache;
    return true;
  } else if (v2hmap->exist_flash_cache) {
    if (hmem->GetFlashCache()->LoadPage(
            fault_page,
            PAGE_SIZE,
            v2hmap->flash_page_offset,
            vaddr_range->vaddress_range_id(),
            (((uint64_t)fault_page - (uint64_t)vaddr_range->address()) >>
             PAGE_BITS)) == false) {
      err("v2hmap shows address %p exists in flash-cache, but cannot read "
          "it.\n",
          fault_page);
      _exit(0);
    }
    ++hit_flash_cache;
    return true;
  } else if (v2hmap->exist_hdd_file) {
    assert(vaddr_range->hdd_file_fd() > 0);
    bool read_ahead = false;
    if (hmem->GetFlashCache()->LoadFromHDDFile(
            vaddr_range, fault_page, v2hmap, read_ahead) == false) {
      err("v2hmap shows address %p exists in flash-cache, but cannot read "
          "it.\n",
          fault_page);
      _exit(0);
    }
    ++hit_hdd_file;
    return true;
  }
  return false;
}

static void SigSegvAction(int sig, siginfo_t* sig_info, void* ucontext) {
  ++number_page_faults;
  uint8_t* fault_address = (uint8_t*)sig_info->si_addr;
  if (!fault_address) {  // Invalid address, shall exit now.
    err("Invalid address=%p\n", fault_address);
    signal(SIGSEGV, SIG_DFL);
    kill(getpid(), SIGSEGV);
    return;
  }

  uint8_t* fault_page = (uint8_t*)((uint64_t)fault_address & PAGE_MASK);
  ucontext_t* context = (ucontext_t*)ucontext;

  // PC pointer value.
  unsigned char* pc = (unsigned char*)context->uc_mcontext.gregs[REG_RIP];

  // rwerror:  0: read, 2: write
  int rwerror = context->uc_mcontext.gregs[REG_ERR] & 0x02;
  if (number_page_faults % 2000000 == 0) {
    dbg("%ld page faults. SIGSEGV at address %p, page %p, pc %p, rw=%d\n",
        number_page_faults,
        fault_address,
        fault_page,
        pc,
        rwerror);
  }

  VAddressRange* vaddr_range = vaddr_range_group.FindVAddressRange(fault_page);
  if (vaddr_range == NULL) {
    err("address=%p not within hybrid-memory range, "
        "forward to default sigsegv.\n",
        fault_address);
    signal(SIGSEGV, SIG_DFL);
    kill(getpid(), SIGSEGV);
    return;
  }
  HybridMemory* hmem =
      hmem_group.GetHybridMemory(fault_address - vaddr_range->address());
  hmem->Lock();
  V2HMapMetadata* v2hmap =
      vaddr_range->GetV2HMapMetadata(fault_address - vaddr_range->address());
  uint64_t  prot_size = PAGE_SIZE;
  if (v2hmap->exist_page_cache) {
    if (rwerror == 0) {
      dbg("Data-race:: virt-address %p already in page cache\n",
          fault_address);
    } else {
      if (mprotect(fault_page, prot_size, PROT_WRITE) != 0) {
        err("in sigsegv: read mprotect %p failed...\n", fault_page);
        perror("mprotect error::  ");
        _exit(0);
      }
      v2hmap->dirty_page_cache = 1;
    }
    hmem->Unlock();
    return;
  }
  if (mprotect(fault_page, prot_size, PROT_WRITE) != 0) {
    err("in sigsegv: read mprotect %p failed...\n", fault_page);
    perror("mprotect error::  ");
    assert(0);
  }
  if (LoadDataFromHybridMemory(fault_page, vaddr_range, hmem, v2hmap) ==
      false) {
    ++unfound_pages;
  } else {
    ++found_pages;
  }
  if (rwerror == 0) {
    if (mprotect(fault_page, prot_size, PROT_READ) != 0) {
      err("in sigsegv: read mprotect %p failed...\n", fault_page);
      perror("mprotect error::  ");
      assert(0);
    }
  }
  bool is_dirty = rwerror ? true : false;
  hmem->GetPageCache()->AddPage(fault_page,
                                prot_size,
                                is_dirty,
                                v2hmap,
                                vaddr_range->vaddress_range_id());
  hmem->Unlock();
  return;
}
