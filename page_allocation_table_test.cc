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
  //pat.ShowStats();
  dbg("level 1 test passed.\n");
}

void TestLevel2() {
  uint64_t total_pages = (3 << 12) + 5;
  dbg("begin level 2 test, total-pages = 0x%lx\n", total_pages);
  PageAllocationTable pat;
  assert(pat.Init("table-1", total_pages) == true);
  //pat.ShowStats();
  dbg("level 2 test passed.\n");
}

void TestLevel3() {
  uint64_t total_pages = (2 << (12 + 8)) + (3 << 12) + 5;
  dbg("begin level 3 test, total-pages = 0x%lx\n", total_pages);
  PageAllocationTable pat;
  assert(pat.Init("table-1", total_pages) == true);
  //pat.ShowStats();
  dbg("level 3 test passed.\n");
}

int main(int argc, char **argv) {
  TestLevel1();
  TestLevel2();
  TestLevel3();
  printf("\nPASS\n");
  return 0;
}
