#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <algorithm>

#include "debug.h"
#include "page_stats_table.h"
#include "hybrid_memory_const.h"

bool PageStatsTable::Init(const std::string& name, uint64_t total_pages) {
  assert(ready_ == false);
  assert(total_pages > 0);

  uint32_t total_bits = 0;
  for (uint64_t i = total_pages - 1; i > 0; i >>= 1) {
    ++total_bits;
  }
  pte_bits_ = PTE_BITS;
  if (total_bits > pte_bits_) {
    pmd_bits_ = (total_bits - pte_bits_)  / 2;
    pgd_bits_ = total_bits - pmd_bits_ - pte_bits_;
  } else {
    pmd_bits_ = 0;
    pgd_bits_ = 0;
  }
  pte_mask_ = (1ULL << pte_bits_) - 1;
  pmd_mask_ = (1ULL << pmd_bits_) - 1;
  pgd_mask_ = (1ULL << pgd_bits_) - 1;

  // One byte count for each page. Init counts are 0 for all pages.
  total_pages_ = total_pages;
  assert(posix_memalign((void**)&pte_entries_, PAGE_SIZE, total_pages_) == 0);
  assert(mlock(pte_entries_, total_pages_) == 0);
  memset(pte_entries_, 0, total_pages_);

  // Allocate PTE nodes. Each PTE node contains (1 << pte_bits_) entries,
  // each entry stands for a page.
  uint64_t entries_per_pte_node = 1ULL << pte_bits_;
  number_pte_nodes_ =
      (total_pages_ + entries_per_pte_node - 1) / entries_per_pte_node;
  ptes_.assign(number_pte_nodes_, PageStatsTableNode<uint8_t>());
  uint8_t* pte_begin_entry = pte_entries_;
  uint64_t remain_entries = total_pages_;
  for (uint64_t i = 0; i < number_pte_nodes_; ++i) {
    uint64_t pte_node_entry_number =
        std::min<uint64_t>(remain_entries, entries_per_pte_node);
    ptes_[i].Init(pte_begin_entry, pte_node_entry_number);
    pte_begin_entry += pte_node_entry_number;
  }

  // Allocate PMD nodes. Each PMD node has (1 << pmd_bits_) entries,
  // with one entry for one PTE node.
  uint64_t entries_per_pmd_node = 1ULL << pmd_bits_;
  number_pmd_nodes_ =
      (number_pte_nodes_ + entries_per_pmd_node - 1) / entries_per_pmd_node;
  pmds_.assign(number_pmd_nodes_, PageStatsTableNode<uint16_t>());
  // PGD node will have "number_pmd_nodes_" entries.
  // All PMD nodes will collectively have "number_pte_nodes_" entries.
  number_pgd_pmd_entries_ = number_pmd_nodes_ + number_pte_nodes_;
  assert(posix_memalign((void**)&pgd_pmd_entries_,
                        PAGE_SIZE,
                        number_pgd_pmd_entries_ * sizeof(uint16_t)) == 0);
  memset(pgd_pmd_entries_, 0, number_pgd_pmd_entries_ * sizeof(uint16_t));

  // Init PGD node.
  pgd_.Init(pgd_pmd_entries_, number_pmd_nodes_);
  for (uint64_t i = 0; i < number_pmd_nodes_; ++i) {
  }

  // Init PMD nodes.
  uint16_t* pmd_begin_entry = pgd_pmd_entries_ + number_pmd_nodes_;
  remain_entries = number_pte_nodes_;
  for (uint64_t i = 0; i < number_pmd_nodes_; ++i) {
    uint64_t pmd_node_entry_number =
        std::min<uint64_t>(remain_entries, entries_per_pmd_node);
    pmds_[i].Init(pmd_begin_entry, pmd_node_entry_number);
    pmd_begin_entry += pmd_node_entry_number;
  }

  name_ = name;
  ready_ = true;
  dbg("PST table %s: %ld pages, pgd_bits=%d, pmd_bits=%d, pte_bits=%d,",
      name_.c_str(), total_pages_, pgd_bits_, pmd_bits_, pte_bits_);
  return ready_;
}

void PageStatsTable::Release() {
  if (ready_) {
    if (pgd_pmd_entries_) {
      free(pgd_pmd_entries_);
      pgd_pmd_entries_ = NULL;
    }
    if (pte_entries_) {
      free(pte_entries_);
      pte_entries_ = NULL;
    }
    ready_ = false;
  }
}

void PageStatsTable::IncreaseAccessCount(uint64_t page_number, uint32_t delta) {
  assert(page_number < total_pages_);

  // Update PTE node.
  uint64_t pte_node_number = (page_number >> pte_bits_);
  uint64_t offset_in_pte_node = (page_number & pte_mask_);
  ptes_[pte_node_number].Increase(offset_in_pte_node, delta);

  // Update PMD node.
  uint64_t pmd_node_number = (page_number >> (pte_bits_ + pmd_bits_));
  uint64_t offset_in_pmd_node = (page_number >> pte_bits_) & pmd_mask_;
  pmds_[pmd_node_number].Increase(offset_in_pmd_node, delta);

  // Update PGD node.
  pgd_.Increase(pmd_node_number, delta);
}

uint64_t PageStatsTable::AccessCount(uint64_t page_number) {
  assert(page_number < total_pages_);
  uint64_t pte_node_number = (page_number >> pte_bits_);
  uint64_t offset_in_pte_node = (page_number & pte_mask_);
  return ptes_[pte_node_number].GetValue(offset_in_pte_node);
}

uint64_t PageStatsTable::PMDAccessCount(uint64_t page_number) {
  assert(page_number < total_pages_);
  uint64_t pmd_node_number = (page_number >> (pte_bits_ + pmd_bits_));
  uint64_t offset_in_pmd_node = (page_number >> pte_bits_) & pmd_mask_;
  return pmds_[pmd_node_number].GetValue(offset_in_pmd_node);
}

uint64_t PageStatsTable::PGDAccessCount(uint64_t page_number) {
  assert(page_number < total_pages_);
  uint64_t pmd_node_number = (page_number >> (pte_bits_ + pmd_bits_));
  return pgd_.GetValue(pmd_node_number);
}

// TODO: Need a better algorithm to select smallest N values and their
// locations.
uint64_t PageStatsTable::FindPagesWithMinCount(uint32_t pages_wanted,
                                               std::vector<uint64_t>* pages) {
  assert(pages_wanted <= (1ULL << pte_bits_));
  uint64_t pmd_node_number = pgd_.GetMinEntryIndex();
  uint64_t relative_pte_node_number = pmds_[pmd_node_number].GetMinEntryIndex();
  uint64_t pte_node_number = (pmd_node_number << pmd_bits_) |
                              relative_pte_node_number;
  // Get the "pages_wanted" pages with smallest count value.
  for (uint64_t i = 0; i < pages_wanted; ++i) {
    uint64_t pos = ptes_[pte_node_number].GetMinEntryIndex();
    pages->push_back((pmd_node_number << (pmd_bits_ + pte_bits_)) |
                    (pte_node_number << pte_bits_) | pos);
    dbg("Get page %ld from pmd_node %ld, pte_node %ld\n",
        pages->back(), pmd_node_number, pte_node_number);
    ptes_[pte_node_number].Set(pos, ptes_[pte_node_number].EntryValueLimit());
    pmds_[pmd_node_number].Increase(relative_pte_node_number, 1);
    pgd_.Increase(pmd_node_number, 1);
  }
  return pages_wanted;
}

void PageStatsTable::ShowStats() {
  printf("\n\nPGD node: page raneg: [0 - %ld)\t", total_pages_);
  pgd_.ShowStats();
  printf("\nPMD nodes: have %ld nodes\n", number_pmd_nodes_);
  for (uint64_t i = 0; i < number_pmd_nodes_; ++i) {
    printf("\tPMD node %ld:  page range: [%ld - %ld)\t",
        i, i << (pmd_bits_ + pte_bits_),
        ((i + 1) << (pmd_bits_ + pte_bits_)) - 1);
    pmds_[i].ShowStats();
  }
  printf("\nPTE nodes: have %ld nodes\n", number_pte_nodes_);
  for (uint64_t i = 0; i < number_pte_nodes_; ++i) {
    printf("\tPTE node %ld: page range: [%ld - %ld)\t",
        i, i << pte_bits_, ((i + 1) << pte_bits_) - 1);
    ptes_[i].ShowStats();
  }
}
