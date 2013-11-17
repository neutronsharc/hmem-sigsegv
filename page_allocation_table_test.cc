// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "page_allocation_table.h"

#include <iostream>

#include "debug.h"

void TestLevel1() {
  PageAllocationTable pat;
  uint64_t total_pages = 17;
  dbg("begin level 1 test, total-pages = 0x%lx\n", total_pages);
  assert(pat.Init("table-1", total_pages) == true);
  pat.ShowStats();
  uint64_t pages[total_pages];

  for (int i = 0; i < total_pages; i++) {
    assert(pat.AllocateOnePage(pages + i) == true);
  }
  assert(pat.AllocateOnePage(pages) == false);
  for (int i = 0; i < total_pages; i++) {
    printf("get pages[%d] = %ld\n", i, pages[i]);
  }
  printf("now release pages\n");
  pat.ShowStats();
  for (int i = 0; i < total_pages; i++) {
    pat.FreePage(pages[i]);
  }
  for (int i = 0; i < total_pages; i++) {
    assert(pat.AllocateOnePage(pages + i) == true);
  }
  assert(pat.AllocateOnePage(pages) == false);
  pat.ShowStats();
  dbg("level 1 test passed.\n");
}

void TestLevel2() {
  uint64_t total_pages = (3 << BITMAP_BITS) + 5;
  uint64_t *pages = new uint64_t[total_pages];
  dbg("begin level 2 test, total-pages = 0x%lx\n", total_pages);
  PageAllocationTable pat;
  assert(pat.Init("table-1", total_pages) == true);
  pat.ShowStats();
  printf("\nNow allocate all pages.\n");
  for (uint64_t i = 0; i < total_pages; i++) {
    assert(pat.AllocateOnePage(&pages[i]) == true);
  }
  pat.ShowStats();
  assert(pat.AllocateOnePage(pages) == false);
  printf("\nNow free all pages.\n");
  for (uint64_t i = 0; i < total_pages; i++) {
    pat.FreePage(pages[i]);
  }
  pat.ShowStats();
  printf("\nalloc all pages again:\n");
  for (uint64_t i = 0; i < total_pages; i++) {
    assert(pat.AllocateOnePage(&pages[i]) == true);
  }
  assert(pat.AllocateOnePage(pages) == false);
  pat.ShowStats();
  dbg("level 2 test passed.\n");
}

void TestLevel3() {
  //uint64_t total_pages = 1024 * 1024 * 16;
  uint64_t total_pages = (3 << 20) | (4 << 12) | 5;
    //1024ULL * 1024 * 1024 * 64 / 4096;
    // 1024 * 1024 * 16;
    //(2 << (BITMAP_BITS + 9)) + (3 << BITMAP_BITS) + 5;
  uint64_t *pages = new uint64_t[total_pages];
  dbg("begin level 3 test, total-pages = 0x%lx (%ld)\n", total_pages,
      total_pages);
  PageAllocationTable pat;
  assert(pat.Init("table-1", total_pages) == true);
  pat.SanityCheck();
  pat.ShowStats();

  printf("\nNow allocate all pages.\n");
  for (uint64_t i = 0; i < total_pages; i++) {
    assert(pat.AllocateOnePage(&pages[i]) == true);
    if (i && i % 1000000 == 0) {
      printf("sanity check at %ld\n", i);
      pat.SanityCheck();
    }
  }
  assert(pat.AllocateOnePage(pages) == false);
  pat.ShowStats();

  printf("\nNow free all pages.\n");
  for (uint64_t i = 0; i < total_pages; i++) {
    pat.FreePage(pages[i]);
    if (i && i % 1000000 == 0) {
      printf("sanity check at %ld\n", i);
      pat.SanityCheck();
    }
  }
  pat.ShowStats();

  struct timespec tstart, tend;
  printf("\nalloc all pages again:\n");
  clock_gettime(CLOCK_REALTIME, &tstart);
  for (uint64_t i = 0; i < total_pages; i++) {
    assert(pat.AllocateOnePage(&pages[i]) == true);
    if (i && i % 1000000 == 0) {
      printf("sanity check at %ld\n", i);
      pat.SanityCheck();
    }
  }
  clock_gettime(CLOCK_REALTIME, &tend);
  int64_t total_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                     (tend.tv_nsec - tstart.tv_nsec);
  printf("%ld allocs, cost %ld ns, %f ns/alloc\n",
         total_pages,
         total_ns,
         (total_ns + 0.0) / total_pages);
  assert(pat.AllocateOnePage(pages) == false);
  pat.ShowStats();

  printf("\nNow free all pages.\n");
  clock_gettime(CLOCK_REALTIME, &tstart);
  for (uint64_t i = 0; i < total_pages; i++) {
    pat.FreePage(pages[i]);
    if (i && i % 1000000 == 0) {
      printf("sanity check at %ld\n", i);
      pat.SanityCheck();
    }
  }
  clock_gettime(CLOCK_REALTIME, &tend);
  total_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                     (tend.tv_nsec - tstart.tv_nsec);
  printf("%ld free, cost %ld ns, %f ns/free-page\n",
         total_pages,
         total_ns,
         (total_ns + 0.0) / total_pages);
  pat.ShowStats();
  dbg("level 3 test passed.\n");
}

void TestLevel3_vector() {
  uint64_t total_pages = 1024 * 1024 * 16;
    //1024ULL * 1024 * 1024 * 64 / 4096;
    // 1024 * 1024 * 16;
    //(2 << (BITMAP_BITS + 9)) + (3 << BITMAP_BITS) + 5;
  uint64_t *all_pages = new uint64_t[total_pages];
  PageAllocationTable pat;
  assert(pat.Init("table-1", total_pages) == true);
  dbg("begin level 3 test, total-pages = 0x%lx (%ld)\n", total_pages,
      total_pages);
  pat.ShowStats();

  uint64_t page_number = 0;
  int alloc_group_size = 16;
  printf("\nNow allocate all pages.\n");
  std::vector<uint64_t> vec_pages;
  for (uint64_t i = 0; i < total_pages; i += alloc_group_size) {
    assert(pat.AllocatePages(alloc_group_size, &vec_pages) == true);
    for (int k = 0; k < alloc_group_size; ++k) {
      all_pages[page_number++] = vec_pages[k];
    }
  }
  assert(pat.AllocateOnePage(all_pages) == false);
  assert(page_number == total_pages);
  pat.ShowStats();

  printf("\nNow free all pages.\n");
  for (uint64_t i = 0; i < total_pages; i++) {
    pat.FreePage(all_pages[i]);
  }
  pat.ShowStats();

  struct timespec tstart, tend;
  printf("\nalloc all pages again:\n");
  clock_gettime(CLOCK_REALTIME, &tstart);
  for (uint64_t i = 0; i < total_pages; i += alloc_group_size) {
    assert(pat.AllocatePages(alloc_group_size, &vec_pages) == true);
  }
  clock_gettime(CLOCK_REALTIME, &tend);
  int64_t total_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                     (tend.tv_nsec - tstart.tv_nsec);
  printf("%ld allocs at %d pgs-unit, cost %ld ns, %f ns/alloc\n",
         total_pages / alloc_group_size,
         alloc_group_size,
         total_ns,
         (total_ns + 0.0) / (total_pages / alloc_group_size));
  assert(pat.AllocateOnePage(all_pages) == false);
  pat.ShowStats();
}

int main(int argc, char **argv) {
  //TestLevel1();
  //TestLevel2();
  TestLevel3();
  //TestLevel3_vector();
  printf("\nPASS\n");
  return 0;
}
