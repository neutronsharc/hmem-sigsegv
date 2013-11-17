#ifndef PAGE_ALLOCATION_TABLE_
#define PAGE_ALLOCATION_TABLE_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "bitmap.h"
#include "hybrid_memory_const.h"

// This struct is a node in the page-alloaction-table tree.
struct PageAllocationTableNode {
  PageAllocationTableNode()
      : number_free_pages_(0),
        number_total_pages_(0),
        number_used_pages_(0),
        entries_(NULL) {}

  ~PageAllocationTableNode() {}

  bool Init(uint64_t* entries,
            uint64_t number_entries,
            uint64_t max_per_entry_pages,
            uint64_t total_pages);

  // Scan this node to find "free_pages_wanted" free pages. These free pages
  // may scatter in multiple childnodes, whose indices are stored in
  // "child_indices" array.  Each child node can contribute some free-pages
  // to sum up to "free_pages_wanted".  The exact amount of each child-node
  // contribution is stored at "child_free_pages" array.
  //
  // Return false if cannot find enough free page.
  bool GetPages(uint64_t free_pages_wanted,
                std::vector<uint64_t>* child_indices,
                std::vector<uint64_t>* child_free_pages);

  void ReleasePages(uint64_t child_index, uint64_t free_pages);

  void ShowStats();

  // Aggregated-stats of all subtrees rooted from this node.
  uint64_t number_free_pages_;
  uint64_t number_used_pages_;
  uint64_t number_total_pages_;

  // Each entry records number of free pages at a child node.
  uint64_t* entries_;

  // Number of child-nodes rooted at this node.
  uint64_t number_entries_;
};

// This class manages page allocation/deallocation.
// A 3-level table struct called Page Allocation Table (PAT) is employed:
// PAT-global-directory (PGD), PAT-middle-directory (PMD), and page bitmap.
//
// Corresponding to the 3-level struct, a page number is divided into
// 3 fields from MSB to LSB:  PGD (pgd_bits_), PMD (pmd_bits_), and page bitmap
// (the location in bitmap indicates index inside PMD).
//
// This class is NOT thread safe.
class PageAllocationTable {
 public:
  PageAllocationTable()
      : ready_(false),
        total_pages_(0),
        bitmaps_(NULL),
        all_pat_entries_(NULL) {}

  virtual ~PageAllocationTable() { Release(); }

  // Prepare internal structs to accommodate the given number of pages.
  // "total_pages" is the number of pages to manage.
  bool Init(const std::string& name, uint64_t total_pages);

  // Free up internal resources.
  bool Release();

  // Allocate "number_of_pages" free pages and store
  // the page numbers to "pages" array.
  bool AllocatePages(uint64_t number_of_pages, std::vector<uint64_t>* pages);

  // Grab one free page.
  bool AllocateOnePage(uint64_t* page);

  // Free up a page.
  void FreePage(uint64_t page);

  void ShowStats();

  // Exam the PGD / PMD / bitmaps in the PAT table to see if its
  // internal structs satisfy the conditions of a well-formed PAT.
  bool SanityCheck();

  uint64_t used_pages() const { return used_pages_; }

  uint64_t free_pages() const { return free_pages_; }

  // Check if the given page number if free.
  bool IsPageFree(uint64_t page);

 protected:
  // Indicate if this class has been initialized.
  bool ready_;

  // How many bits covered at PGD / PMD / bitmap level.
  uint32_t pgd_bits_;
  uint32_t pmd_bits_;
  uint32_t bitmap_bits_;

  // Value mask at each level.
  uint64_t pgd_mask_;
  uint64_t pmd_mask_;
  uint64_t bitmap_mask_;

  // How many levels in this PAT table. Can be 1/2/3.
  uint32_t levels_;

  // Level 1: Root of the PAT tree.
  PageAllocationTableNode pgd_;

  // Level 2: array of nodes as children in PAT tree.
  std::vector<PageAllocationTableNode> pmds_;
  uint64_t number_pmd_nodes_;

  // Level 3: array of bitmaps.
  Bitmap<(1ULL << BITMAP_BITS)>* bitmaps_;
  uint64_t number_bitmaps_;

  // A pre-allocated buffer pool from where "entries_" array is grabbed
  // to assign to each instance of PageAllocationTableNode.
  uint64_t* all_pat_entries_;
  uint64_t number_all_pat_entries_;

  // Total number of pages.
  uint64_t total_pages_;

  uint64_t used_pages_;

  uint64_t free_pages_;

  std::string name_;
};

#endif  // PAGE_ALLOCATION_TABLE_
