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
#include "page_allocation_table.h"

bool PageAllocationTableNode::Init(
    uint64_t* entries, uint64_t number_entries,
    uint64_t max_per_entry_pages, uint64_t total_pages) {
  assert(entries != NULL);
  assert(max_per_entry_pages * (number_entries - 1) < total_pages);
  assert(total_pages <= max_per_entry_pages * number_entries);

  entries_ = entries;
  uint64_t remain_pages = total_pages;
  for (uint64_t i = 0; remain_pages > 0 && i < number_entries; ++i) {
    entries_[i] = std::min<uint64_t>(max_per_entry_pages, remain_pages);
    remain_pages -= entries_[i];
  }
  number_entries_ = number_entries;
  number_free_pages_ = total_pages;
  number_total_pages_ = total_pages;
  number_used_pages_ = 0;
}

bool PageAllocationTableNode::GetPages(
    uint64_t free_pages_wanted, std::vector<uint64_t>*
    child_indices, std::vector<uint64_t>* child_free_pages) {
  if (free_pages_wanted == 0) {
    return true;
  }
  if (free_pages_wanted > number_free_pages_) {
    err("Unable to alloc %ld free pages from %ld avail-pages\n",
        free_pages_wanted, number_free_pages_);
    return false;
  }
  // 1. Try to find a first-match child that can satisfy this request.
  for (uint64_t i = 0; i < number_entries_; ++i ) {
    if (entries_[i] >= free_pages_wanted) {
      child_indices->push_back(i);
      child_free_pages->push_back(free_pages_wanted);
      entries_[i] -= free_pages_wanted;
      number_free_pages_ -= free_pages_wanted;
      number_used_pages_ += free_pages_wanted;
      return true;
    }
  }
  // 2. If no single child-node contains enough free-pages, we have to
  // touch multiple child-nodes.
  uint64_t remain_wanted_pages = free_pages_wanted;
  for (uint64_t i = 0; i < number_entries_ && remain_wanted_pages > 0; ++i) {
    if (entries_[i] > 0) {
      child_indices->push_back(i);
      uint64_t get_pages = std::min<uint64_t>(entries_[i], remain_wanted_pages);
      entries_[i] -= get_pages;
      child_free_pages->push_back(get_pages);
      remain_wanted_pages -= get_pages;
    }
  }
  assert(remain_wanted_pages == 0);
  number_free_pages_ -= free_pages_wanted;
  number_used_pages_ += free_pages_wanted;
  return true;
}

// Release "free_pages" pages to a child node with index "child_index".
void PageAllocationTableNode::ReleasePages(
    uint64_t child_index, uint64_t free_pages) {
  assert(child_index < number_entries_);
  entries_[child_index] += free_pages;
  number_free_pages_ += free_pages;
  number_used_pages_ -= free_pages;
  assert(number_free_pages_ <= number_total_pages_);
}

void PageAllocationTableNode::ShowStats() {
  fprintf(stderr, "%ld entries, total-pages=%ld, "
      "free-pages=%ld, used-paegs=%ld\n",
      number_entries_, number_total_pages_, number_free_pages_,
      number_used_pages_);
  for (uint64_t i = 0; i < number_entries_; ++i) {
    fprintf(stderr, "entry[%ld] = %ld\n", i, entries_[i]);
  }
}

bool PageAllocationTable::Init(const std::string& name, uint64_t total_pages) {
  assert(total_pages > 0);
  uint32_t total_bits = 0;
  for (uint64_t i = total_pages - 1; i > 0; i >>= 1) {
    ++total_bits;
  }

  if (total_bits <= BITMAP_BITS) {
    pgd_bits_ = 0;
    pmd_bits_ = 0;
    bitmap_bits_ = total_bits;
    levels_ = 1;
    dbg("%s: PAT table has only 1 level: 0-0-%d\n", name.c_str(), bitmap_bits_);
  } else if (total_bits <= BITMAP_BITS + 8) {
    pgd_bits_ = total_bits - BITMAP_BITS;
    pmd_bits_ = 0;
    bitmap_bits_ = BITMAP_BITS;
    levels_ = 2;
    dbg("%s: PAT table has 2 level: %d-%d-%d\n", name.c_str(), pgd_bits_,
        pmd_bits_, bitmap_bits_);
  } else {
    bitmap_bits_ = BITMAP_BITS;
    pgd_bits_ = (total_bits - BITMAP_BITS) / 2;
    pmd_bits_ = total_bits - pgd_bits_ - bitmap_bits_;
    levels_ = 3;
    dbg("%s: PAT table has 3 level: %d-%d-%d\n", name.c_str(), pgd_bits_,
        pmd_bits_, bitmap_bits_);
  }
  pgd_mask_ = (1ULL << pgd_bits_) - 1;
  pmd_mask_ = (1ULL << pmd_bits_) - 1;
  bitmap_mask_ = (1ULL << bitmap_bits_) - 1;

  uint64_t pages_per_bitmap = 1 << BITMAP_BITS;
  number_bitmaps_ =
    (total_pages + pages_per_bitmap - 1) / pages_per_bitmap;
  bitmaps_ = new Bitmap<(1ULL<<BITMAP_BITS)>[number_bitmaps_];
  assert(bitmaps_ != NULL);
  assert(mlock(
        bitmaps_, number_bitmaps_ * sizeof(Bitmap<(1ULL<<BITMAP_BITS)>)) == 0);
  for (uint64_t i = 0; i < number_bitmaps_; ++i) {
    bitmaps_[i].set_all();
  }
  // The last bitmap may not be fully mapped to existing pages.
  // Mask out trailing non-exist pages.
  if (total_pages % pages_per_bitmap != 0) {
    for (uint64_t i = total_pages % pages_per_bitmap + 1;
        i <= pages_per_bitmap; ++i) {
      bitmaps_[number_bitmaps_ - 1].clear(i);
    }
  }

  if (levels_ == 1 ) {
    assert(bitmaps_[0].number_of_set_bits() == total_pages);
  } else if (levels_ == 2) {
    number_all_pat_entries_ = number_bitmaps_;
    all_pat_entries_ = new uint64_t[number_all_pat_entries_];
    assert(all_pat_entries_ != NULL);
    pgd_.Init(all_pat_entries_, number_all_pat_entries_,
        pages_per_bitmap, total_pages);
    // The last bitmap may not be entirely mapped to existing pages,
    // so additional care is needed to mask out non-existing pages.
    for (uint64_t i = 0; i < number_all_pat_entries_; ++i) {
      assert(pgd_.entries_[i] == bitmaps_[i].number_of_set_bits());
    }
  } else {
    uint64_t entries_per_pmd_node = (1 << pmd_bits_);
    number_pmd_nodes_ = (number_bitmaps_ + (entries_per_pmd_node - 1))
      / entries_per_pmd_node; // entries at PGD
    number_all_pat_entries_ = number_bitmaps_  // entries at PMD level
      +  number_pmd_nodes_; // entries at PGD

    all_pat_entries_ = new uint64_t[number_all_pat_entries_];
    assert(all_pat_entries_ != NULL);
    pgd_.Init(all_pat_entries_, number_pmd_nodes_,
        entries_per_pmd_node * pages_per_bitmap, total_pages);

    pmds_.assign(number_pmd_nodes_, PageAllocationTableNode());

    uint64_t* entry_pos = all_pat_entries_ + number_pmd_nodes_;
    uint64_t remain_entries = number_bitmaps_;
    uint64_t remain_pages = total_pages;
    uint64_t max_pages_per_pmd_node = entries_per_pmd_node * pages_per_bitmap;

    for (uint64_t i = 0; i < number_pmd_nodes_; ++i) {
      uint64_t entries_in_this_pmd_node =
        std::min<uint64_t>(remain_entries, entries_per_pmd_node);
      uint64_t pages_in_this_pmd_node =
        std::min<uint64_t>(remain_pages, max_pages_per_pmd_node);
      pmds_[i].Init(entry_pos, entries_in_this_pmd_node, pages_per_bitmap,
          pages_in_this_pmd_node);
      entry_pos += entries_in_this_pmd_node;
      remain_entries -= entries_in_this_pmd_node;
      remain_pages -= pages_in_this_pmd_node;
    }
  }

  uint64_t cnt = 0;
  for (uint64_t i = 0; i < number_bitmaps_; ++i) {
    cnt += bitmaps_[i].number_of_set_bits();
  }
  assert(cnt == total_pages);

  total_pages_ = total_pages;
  used_pages_ = 0;
  free_pages_ = total_pages_;
  name_ = name;
  ready_ = true;
  return ready_;
}

bool PageAllocationTable::Release() {
  if (ready_) {
    if (all_pat_entries_) {
      delete all_pat_entries_;
    }
    if (bitmaps_) {
      delete bitmaps_;
    }
    ready_ = false;
  }
  return true;
}

bool PageAllocationTable::AllocateOnePage(uint64_t *page) {
  if (free_pages_ == 0) {
    err("PAT \"%s\": No more free pages available.\n", name_.c_str());
    return false;
  }
  std::vector<uint64_t> pages;
  if (AllocatePages(1, &pages) == true) {
    *page = pages[0];
    return true;
  }
  return false;
}

bool PageAllocationTable::AllocatePages(
    uint64_t number_of_pages, std::vector<uint64_t>* pages) {
  if (free_pages_ < number_of_pages) {
    err("PAT \"%s\": cannot alloc %ld pages, free-pages = %ld\n",
        name_.c_str(), number_of_pages, free_pages_);
    return false;
  }
  pages->clear();
  if (levels_ == 1) {
    for (uint64_t i = 0; i < number_of_pages; ++i) {
      uint64_t page_number = bitmaps_[0].ffs(true);
      assert(page_number > 0);
      pages->push_back(page_number - 1);
    }
  } else if (levels_ == 2) {
    std::vector<uint64_t> bitmap_indices;
    std::vector<uint64_t> bitmap_free_pages;
    assert(pgd_.GetPages(number_of_pages, &bitmap_indices, &bitmap_free_pages) ==
        true);
    for (uint64_t i = 0; i < bitmap_indices.size(); ++i) {
      uint64_t bitmap_index = bitmap_indices[i];
      for (uint64_t j = 0; j < bitmap_free_pages[i]; j++) {
        uint64_t page_number = bitmaps_[bitmap_index].ffs(true);
        assert(page_number > 0);
        pages->push_back((bitmap_index << bitmap_bits_) + (page_number - 1));
      }
    }
  } else {
    std::vector<uint64_t> pmd_indices;
    std::vector<uint64_t> pmd_free_pages;
    assert(pgd_.GetPages(number_of_pages, &pmd_indices, &pmd_free_pages) ==
        true);
    for (uint64_t i = 0; i < pmd_indices.size(); ++i) {
      uint64_t pmd_index = pmd_indices[i];
      uint64_t free_pages_from_pmd = pmd_free_pages[i];
      std::vector<uint64_t> bitmap_indices;
      std::vector<uint64_t> bitmap_free_pages;
      assert(pmds_[pmd_index].GetPages(
            free_pages_from_pmd, &bitmap_indices, &bitmap_free_pages) == true);
      for (uint64_t j = 0; j < bitmap_indices.size(); ++j) {
        uint64_t bitmap_index =
          (pmd_index << pmd_bits_ ) | bitmap_indices[j];
        for (uint64_t k = 0; k < bitmap_free_pages[j]; ++k) {
          uint64_t page_number = bitmaps_[bitmap_index].ffs(true);
          if (page_number ==0) {
            err("err!\n");
            assert(0);
          }
          pages->push_back((pmd_index << (pmd_bits_ + bitmap_bits_)) |
              (bitmap_index << bitmap_bits_) | (page_number - 1));
        }
      }
    }
  }
  free_pages_ -= number_of_pages;
  used_pages_ += number_of_pages;
  return pages->size() == number_of_pages;
}

void PageAllocationTable::FreePage(uint64_t page) {
  assert(page >= 0 && page < total_pages_);
  if (levels_ == 1) {
    bitmaps_[0].set(page + 1);
  } else if (levels_ == 2) {
    uint64_t bitmap_index = page >> bitmap_bits_;
    uint64_t page_offset = page & ((1ULL << bitmap_bits_) - 1);
    // bitmap uses page number [1, total-pages].
    bitmaps_[bitmap_index].set(page_offset + 1);
    pgd_.ReleasePages(bitmap_index, 1);
  } else {
    uint64_t pmd_index = (page >> (bitmap_bits_ + pmd_bits_)) & pgd_mask_;
    uint64_t bitmap_index = page >> bitmap_bits_;
    uint64_t page_offset = page & bitmap_mask_;
    // bitmap uses page number [1, total-pages].
    bitmaps_[bitmap_index].set(page_offset + 1);
    pmds_[pmd_index].ReleasePages(bitmap_index & pmd_mask_, 1);
    pgd_.ReleasePages(pmd_index, 1);
  }
  ++free_pages_;
  --used_pages_;
}

void PageAllocationTable::ShowStats() {
  fprintf(stderr, "********\n"
      "PAT \"%s\", %d levels, pgd-pmd-bitmap = %d.%d.%d\n",
      name_.c_str(), levels_, pgd_bits_, pmd_bits_, bitmap_bits_);
  fprintf(stderr, "Total-pages = %ld, free-pages = %ld, used-pages=%ld\n",
      total_pages_, free_pages_, used_pages_);
  fprintf(stderr, "The last bitmap is:\n%s\n",
      bitmaps_[number_bitmaps_ - 1].to_string().c_str());
  if (levels_ == 1) {
    fprintf(stderr, "\n");
  } else if (levels_ == 2) {
    fprintf(stderr, "\n");
  } else {
    fprintf(stderr, "\n");
  }
}
