#include <assert.h>
#include <stdio.h>
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
  uint64_t total_pages = 1024 * 1024 * 16;
    //1024ULL * 1024 * 1024 * 64 / 4096;
    // 1024 * 1024 * 16;
    //(2 << (BITMAP_BITS + 9)) + (3 << BITMAP_BITS) + 5;
  uint64_t *pages = new uint64_t[total_pages];
  dbg("begin level 3 test, total-pages = 0x%lx (%ld)\n", total_pages,
      total_pages);
  PageAllocationTable pat;
  assert(pat.Init("table-1", total_pages) == true);
  pat.ShowStats();

  printf("\nNow allocate all pages.\n");
  for (uint64_t i = 0; i < total_pages; i++) {
    assert(pat.AllocateOnePage(&pages[i]) == true);
  }
  assert(pat.AllocateOnePage(pages) == false);
  pat.ShowStats();

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

  dbg("level 3 test passed.\n");
}

void TestLevel3_vector() {
  uint64_t total_pages = 1024 * 1024 * 16;
    //1024ULL * 1024 * 1024 * 64 / 4096;
    // 1024 * 1024 * 16;
    //(2 << (BITMAP_BITS + 9)) + (3 << BITMAP_BITS) + 5;
  uint64_t *all_pages = new uint64_t[total_pages];
  dbg("begin level 3 test, total-pages = 0x%lx (%ld)\n", total_pages,
      total_pages);
  PageAllocationTable pat;
  assert(pat.Init("table-1", total_pages) == true);
  pat.ShowStats();

  printf("\nNow allocate all pages.\n");
  std::vector<uint64_t> vec_pages;
  for (uint64_t i = 0; i < total_pages; i += 64) {
    assert(pat.AllocatePages(64, &vec_pages) == true);
    //all_pages.append(vec_pages);
  }
  assert(pat.AllocateOnePage(all_pages) == false);
  pat.ShowStats();
}

int main(int argc, char **argv) {
  //TestLevel1();
  //TestLevel2();
  //TestLevel3();
  TestLevel3_vector();
  printf("\nPASS\n");
  return 0;
}
