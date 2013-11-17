// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include <assert.h>
#include <stdio.h>
#include "page_stats_table.h"

static void TestPageAccessTable() {
  PageStatsTable  pst;
  uint64_t number_of_pages = 1 << 9; // 3 + 3 + 4
  assert(pst.Init("pg-stats-table", number_of_pages) == true);

  for (uint64_t i = 0; i < number_of_pages; i++) {
    pst.IncreaseAccessCount(i, (number_of_pages - i + 1) % 255);
  }
  for (uint64_t i = 0; i < number_of_pages; i++) {
    assert(pst.AccessCount(i) == (number_of_pages - i + 1) % 255);
  }
  pst.ShowStats();

  std::vector<uint64_t> pages_min_access;
  uint64_t pages_wanted = 8;
  pst.FindPagesWithMinCount(pages_wanted, &pages_min_access);

  for (uint64_t i = 0; i < pages_wanted; i++) {
    printf("min-%ld: page %ld, value = %ld\n",
           i, pages_min_access[i], pst.AccessCount(pages_min_access[i]));
  }
  pst.ShowStats();


  //for (uint64_t i = 0; i < pages_wanted; i++) {
  for (uint64_t i = 0; i < 1; i++) {
    uint64_t pnum = pages_min_access[0];
    uint64_t pgd_access_count = pst.PGDAccessCount(pnum);
    uint64_t pmd_access_count = pst.PMDAccessCount(pnum);
    printf("\n\nNow inc page %ld access count by 1\n", pnum);
    pst.IncreaseAccessCount(pages_min_access[0], 1);
    assert(pgd_access_count + 1 == pst.PGDAccessCount(pnum));
    assert(pmd_access_count + 1 == pst.PMDAccessCount(pnum));
  }
  pst.ShowStats();
}

int main(int argc, char **argv) {
  TestPageAccessTable();
  printf("PASS\n");
  return 0;
}
