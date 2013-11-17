// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "vaddr_range.h"
#include "avl.h"

static void Test2() {
  V2HMapMetadata v2hmap;  // sizeof() = 5B
  printf("sizeof v2hmap = %ld\n", sizeof(v2hmap));

  memset(&v2hmap, 0, sizeof(v2hmap));
  //assert(v2hmap.hmem_id == 0);
  assert(v2hmap.flash_page_offset == 0);

  //v2hmap.hmem_id = 32;
  v2hmap.flash_page_offset = 165;

  //assert(v2hmap.hmem_id == 32);
  assert(v2hmap.flash_page_offset == 165);

  printf("Test succeeded.\n");
}

static void TestVaddrRange() {
  VAddressRangeGroup vaddr_group;

  vaddr_group.Init();
  uint32_t total_ranges = 10;//vaddr_group.GetTotalVAddressRangeNumber();

  VAddressRange *vranges[total_ranges];
  uint64_t vrange_size = 4096ULL * 1000 * 200;

  for (uint32_t i = 0; i < total_ranges; ++i) {
    vranges[i] = vaddr_group.AllocateVAddressRange(vrange_size);
    assert(vranges[i] != NULL);
  }
  //assert(vaddr_group.AllocateVAddressRange(vrange_size) == NULL);
  assert(vaddr_group.GetFreeVAddressRangeNumber() ==
         vaddr_group.GetTotalVAddressRangeNumber() - total_ranges);

  for (uint32_t i = 0; i < total_ranges; ++i) {
    uint8_t *p = vranges[i]->address() + 0x1234;
    VAddressRange *vaddr_range  = vaddr_group.FindVAddressRange(p);
    assert(vaddr_range == vranges[i]);
    vaddr_group.ReleaseVAddressRange(vranges[i]);
    vranges[i] = NULL;
  }
  assert(vaddr_group.GetFreeVAddressRangeNumber() ==
         vaddr_group.GetTotalVAddressRangeNumber());

  for (uint32_t i = 0; i < total_ranges; ++i) {
    vranges[i] = vaddr_group.AllocateVAddressRange(vrange_size);
    assert(vranges[i] != NULL);
  }
  //assert(vaddr_group.AllocateVAddressRange(vrange_size) == NULL);
  assert(vaddr_group.GetFreeVAddressRangeNumber() ==
         vaddr_group.GetTotalVAddressRangeNumber() - total_ranges);

  for (uint32_t i = 0; i < total_ranges; ++i) {
    uint8_t *p = vranges[i]->address() + 0x1234;
    VAddressRange *vaddr_range  = vaddr_group.FindVAddressRange(p);
    assert(vaddr_range == vranges[i]);
    vaddr_group.ReleaseVAddressRange(vranges[i]);
    vranges[i] = NULL;
  }
  assert(vaddr_group.GetFreeVAddressRangeNumber() ==
         vaddr_group.GetTotalVAddressRangeNumber());
}

int main(int argc, char **argv) {
  Test2();
  //TestVaddrRange();
  return 0;
}
