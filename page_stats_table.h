// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef PAGE_STATS_TABLE_
#define PAGE_STATS_TABLE_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "debug.h"
#include "bitmap.h"
#include "hybrid_memory_const.h"

// This struct represent a PGD/PMD/PTE node in the PST tabe.
template <typename T>
class PageStatsTableNode {
 public:
  PageStatsTableNode()
      : entries_(NULL),
        number_entries_(0),
        entry_value_limit_(0),
        last_entry_needs_compensation_(false),
        last_entry_compensation_(1.0) {}

  ~PageStatsTableNode() {}

  // Initialzie the node.
  // This class does NOT own the pointer.
  bool Init(T* entries, uint64_t number_entries) {
    entries_ = entries;
    number_entries_ = number_entries;
    entry_value_limit_ = (1ULL << sizeof(T) * 8) - 1;
    compare_entries_.assign(number_entries_,
                            std::pair<uint64_t, uint64_t>(0, 0));
  }

  // Increate the value at entry at "index".
  void Increase(uint64_t index, T delta) {
    assert(index < number_entries_);
    assert(delta <= entry_value_limit_);
    while (((uint64_t)entries_[index] + delta > entry_value_limit_)) {
      ShiftRight(1);
    }
    entries_[index] += delta;
  }

  // Decrease the value at entry at "index".
  void Decrease(uint64_t index, T delta) {
    assert(index < number_entries_);
    // It's possible entries[index] < delta because of
    // ShiftRight() ops caused by inc at other entries.
    if (entries_[index] > delta) {
      entries_[index] -= delta;
    } else {
      entries_[index] = 0;
    }
  }

  // Assign the given value to the "index" entry.
  void Set(uint64_t index, T value) {
    assert(index < number_entries_);
    assert(value <= entry_value_limit_);
    entries_[index] = value;
  }

  // When an entry value is about to overflow, right-shift all entries
  // by given bits to prevent overflow.
  void ShiftRight(uint32_t bits) {
    for (uint64_t i = 0; i < number_entries_; ++i) {
      entries_[i] >>= 1;
    }
  }

  // Return the index of the entry of min value.
  // The index is relative offset of the entry in this node.
  uint64_t GetMinEntryIndex() {
    uint64_t min_index = 0;
    if (number_entries_ == 1) {
      return min_index;
    }
    uint64_t min_value = entry_value_limit_;
    for (uint64_t i = 0; i < number_entries_ - 1; ++i) {
      if (min_value > entries_[i]) {
        min_value = entries_[i];
        min_index = i;
      }
    }
    uint64_t last_entry_value = entries_[number_entries_ - 1];
    if (last_entry_needs_compensation_) {
      last_entry_value =
          std::min(entry_value_limit_,
                   (uint64_t)(last_entry_compensation_ * last_entry_value));
    }
    if (min_value > last_entry_value) {
      min_index = number_entries_ - 1;
    }
    return min_index;
  }

  T GetValue(uint64_t index) {
    assert(index < number_entries_);
    return entries_[index];
  }

  // Display stats.
  void ShowStats() {
    printf("%ld entries, max-value=%ld\n", number_entries_, entry_value_limit_);
    for (uint64_t i = 0; i < number_entries_; ++i) {
      printf("%ld ", (uint64_t)entries_[i]);
    }
    printf("\n");
  }

  void Dump(uint64_t start_index, uint64_t number_entries) {
    assert((start_index < number_entries_) &&
           (start_index + number_entries <= number_entries_));
    printf("This stat-node has %ld entries.\n", number_entries_);
    for (uint64_t i = start_index; i < start_index + number_entries; ++i) {
      printf("[%ld]: %ld  ", i, (uint64_t)entries_[i]);
    }
    printf("\n");
  }

  uint64_t EntryValueLimit() { return entry_value_limit_;}

  void AssignChildNodes(PageStatsTableNode* child_nodes) {
    child_nodes_ = child_nodes;
  }

  void set_last_entry_compensation(double last_entry_compensation) {
    last_entry_compensation_ = last_entry_compensation;
  }

  void set_last_entry_needs_compensation(bool needs_compensation) {
    last_entry_needs_compensation_ = needs_compensation;
  }

  // Find the smallest N entries and return their in-node position in
  // "position" array.
  // "entries_wanted" is the number of entries needed to get.
  void GetSmallestEntryPositions(uint32_t entries_wanted,
                                 std::vector<uint64_t>* positions) {
    assert(entries_wanted <= number_entries_);
    assert(entries_wanted <= compare_entries_.size());
    for (uint64_t i = 0; i < number_entries_; ++i) {
      compare_entries_[i].first = entries_[i];
      compare_entries_[i].second = i;
    }
    std::sort(compare_entries_.begin(), compare_entries_.end(), sort_entries);
    positions->clear();
    for (uint64_t i = 0; i < entries_wanted; ++i) {
      positions->push_back(compare_entries_[i].second);
    }
  }

 protected:
  // Sort all entries in ascending order of entry value.
  static bool sort_entries(const std::pair<uint64_t, uint64_t>& e1,
                           const std::pair<uint64_t, uint64_t>& e2) {
    // This compare must be a strict weak ordering.
    return e1.first < e2.first;
  }
  // Each entry records access stats of all pags at a child node.
  T* entries_;

  // Use this list of pairs to sort all entries. Each pair represents
  // an entry value and its position inside "entries_" array.
  std::vector<std::pair<uint64_t, uint64_t> > compare_entries_;

  // Size of "entries_" array.
  uint64_t number_entries_;

  // An entry value cannot exceed this limit.
  uint64_t entry_value_limit_;

  // Pointers to child nodes.
  // This field isn't used by PTE nodes because it's children
  // are represented by "entries_" array.
  PageStatsTableNode* child_nodes_;

  // The last entry may point to a partially-filled tree, so its
  // conuters needs compensation to make a fair comparison against
  // other entries.
  bool last_entry_needs_compensation_;

  double last_entry_compensation_;
};


// This class implements a mechanism to record the access frequency
// of a group of pages.
//
// A 3-level table struct called Page Stats Table (PST) is employed:
// PST-global-directory (PGD), PST-middle-directory (PMD),
// and PST-Table-Entry (PTE). Each entry at every level of PST represents
// the access frequency of region/sub-region/page, respectively.
//
// An entry in PGD sum of access counts of all pages
// in a PMD node rooted at that PGD entry.
//
// An entry in PMD node records sum of access counts of all pages
// in a PTE node rooted at that PMD entry.
//
// A PTE is an array of 4K uint8 values recording access counts of a
// sequence of pages.
//
// Corresponding to the 3-level struct, a page number is divided into
// 3 fields from MSB to LSB:  PGD (pgd_bits_), PMD (pmd_bits_),
// and PTE (pte_bits_).
//
// This class is NOT thread safe.
class PageStatsTable {
 public:
  PageStatsTable() : ready_(false), pgd_pmd_entries_(NULL), pte_entries_(NULL) {}

  virtual ~PageStatsTable() { Release(); }

  // Prepare internal structs to accommodate the given number of pages.
  // "total_pages" is the number of pages to manage.
  bool Init(const std::string& name, uint64_t total_pages);

  // Free up internal resources.
  void Release();

  // Increate the access count of the given page by "delta" value.
  void IncreaseAccessCount(uint64_t page_number, uint32_t delta);

  // Find the access count at the given page.
  uint64_t AccessCount(uint64_t page_number);

  // Get the access count at the parent PMD entry that encloses
  // the page.
  uint64_t PMDAccessCount(uint64_t page_number);

  // Get the access count at the grand-parent PGD entry that encloses
  // the page.
  uint64_t PGDAccessCount(uint64_t page_number);

  void ShowStats();

  // Search the stats table to get a list of page number whose
  // access counts are the smallest. These page numbers
  // are stored in "pages" array.
  // Caller owns the pointer.
  uint64_t FindPagesWithMinCount(uint32_t pages_wanted,
                                 std::vector<uint64_t>* pages);

  // Exam the PGD / PMD / PET in the PST table to see if its
  // internal structs satisfy the conditions of a well-formed PST.
  bool SanityCheck();

  uint8_t* GetPTEEntries() { return pte_entries_; }

  uint16_t* GetPGDPMDEntries() { return pgd_pmd_entries_; }

  // Pre-allocated contiguous memory for PGD and PMD nodes' internal entries.
  uint16_t* pgd_pmd_entries_;

  // Level 3: array of pte entries, one byte per page.
  uint8_t* pte_entries_;

 protected:
  // Indicate if this class has been initialized.
  bool ready_;

  // How many bits covered at PGD / PMD / PET.
  uint32_t pgd_bits_;
  uint32_t pmd_bits_;
  uint32_t pte_bits_;

  // Value mask at each level.
  uint64_t pgd_mask_;
  uint64_t pmd_mask_;
  uint64_t pte_mask_;

  // Level 1: Root of the PST tree.
  PageStatsTableNode<uint16_t> pgd_;

  // Level 2: array of nodes at PMD level.
  std::vector<PageStatsTableNode<uint16_t> > pmds_;
  uint64_t number_pmd_nodes_;

  // Size of "pgd_pmd_entries_" array, == number_pmd_nodes_ + number_pte_nodes_.
  uint64_t number_pgd_pmd_entries_;

  // Level 3: array of nodes as children in PAT tree.
  std::vector<PageStatsTableNode<uint8_t> > ptes_;
  uint64_t number_pte_nodes_;

  // Total number of pages, also size of "ptes_" array.
  uint64_t total_pages_;

  std::string name_;
};

#endif  // PAGE_STATS_TABLE_
