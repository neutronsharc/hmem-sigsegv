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
  assert(posix_memalign(&pte_entries_, PAGE_SIZE, total_pages_) == 0);
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
  pmds_.assign(number_pmd_nodes_, PageStatsTableNode<uint16_t>());
  uint64_t entries_per_pmd_node = 1ULL << pmd_bits_;
  number_pmd_nodes_ =
      (number_pte_nodes_ + entries_per_pmd_node - 1) / entries_per_pmd_node;
  // PGD node will have "number_pmd_nodes_" entries.
  // All PMD nodes will collectively have "number_pte_nodes_" entries.
  number_pgd_pmd_entries_ = number_pmd_nodes_ + number_pte_nodes_;
  assert(posix_memalign(&pgd_pmd_entries_,
                        PAGE_SIZE,
                        number_pgd_pmd_entries_ * sizeof(uint16_t)) == 0);
  memset(pgd_pmd_entries_, 0, number_pgd_pmd_entries_ * sizeof(uint16_t));

  // Init PGD node.
  uint64_t entries_per_pgd_node = 1ULL << pmd_bits_;
  pgd_.Init(pgd_pmd_entries_, number_pmd_nodes_);

  // Init PMD nodes.
  uint64_t entries_per_pmd_node = 1ULL << pmd_bits_;
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
  return ready;
}

void PageStatsTable::Release() {
  if (ready_) {
    if (pgd_pmd_entries_) {
      free(pgd_pmd_entries_);
      pgd_pmd_entries_ = NULL;
    }
    if (ptes_) {
      free(ptes_);
      ptes_ = NULL;
    }
    ready_ = false;
  }
}

void PageStatsTable::Increase(uint64_t page_number, uint32_t delta) {
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

uint64_t FindPagesWithMinCount(uint32_t pages_wanted, vector<uint64_t>* pages) {
  assert(pages_wanted <= (1ULL << pte_bits_));
  uint64_t pmd_node_number = pgd_.GetMinEntryIndex();
  uint64_t pte_node_number = pmds_[pmd_node_number].GetMinEntryIndex();

  // Get the "pages_wanted" pages with smallest count value.

}
