#ifndef VADDR_RANGE_H_
#define VADDR_RANGE_H_

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <bitset>

#include "avl.h"
#include "hybrid_memory_const.h"

// Vritual-address to hybrid-memory mapping metadata to record
// mapping information for each virtual-page.
struct V2HMapMetadata {
  uint32_t exist_page_cache : 1;
  uint32_t exist_ram_cache : 1;
  uint32_t exist_ssd_cache : 1;
  uint32_t exist_hdd_file : 1;
  uint32_t dirty_page_cache : 1;
  uint32_t dirty_ram_cache : 1;
  uint32_t dirty_ssd_cache : 1;
  uint32_t reserved : 1;

  uint32_t hmem_id : 8;

  // If the virt-page has a copy in ssd, this is the page-offset
  // inside the flash-cache in the hmem identified by "hmem_id".
  uint32_t ssd_page_offset : 24;
} __attribute__((__packed__));

// This class represents a virtual address range.
//
// Each vrange is created by user calling hmem_mmap() or hmem_malloc().
//
// All ranges are sorted to a BST, such that we can quickly locate
// a vaddr_range given an arbitrary virtual address.
class VAddressRange {
 public:
  VAddressRange();
  virtual ~VAddressRange();

  // Activate this vaddr-range by allocating virtual address and
  // initing its internal structs.
  bool Init(uint64_t size);

  // Release internal structs.
  void Release();

  AVLNode *GetTreeNode() { return &avl_node_; }

  uint8_t *GetAddress() { return address_; }

  // id of this vaddr_range.
  uint32_t vaddr_range_id_;

  // Find the v2hmap entry for an address within this vaddr-range
  // with offset = "address_offset".
  V2HMapMetadata* GetV2HMapMetadata(uint64_t address_offset);

 protected:
  // If true, this vaddr-range has been allocated and is being used.
  bool is_active_;

  // Stating address of this range.
  uint8_t *address_;

  // Total byte-size of this vaddr_range.
  uint64_t size_;

  // How many pages in this vaddr-range.
  uint64_t number_pages_;

  // A tree-node to link this vaddr-range to a BST.
  AVLNode avl_node_;

  // The underlying hdd file that backs this vaddr range.
  // Used when doing mmap().
  // vaddr-range has a 1-on-1 mapping to the backing hdd file.
  std::string hdd_file_name_;

  // This vaddr-range maps to backing hdd file from this
  // byte-offset onwards and extends to "size_" bytes.
  uint64_t hdd_file_offset_;

  // An array of metadata record, one entry per virt-page.
  V2HMapMetadata *v2h_map_;

  // Byte-size of the v2hmap array.
  uint64_t v2h_map_size_;
};


// This class includes a BST that sorts all vaddr_ranges.
class VAddressRangeGroup {
 public:
  VAddressRangeGroup();
  virtual ~VAddressRangeGroup();

  // Init internal structs of the vaddr-groups.
  bool Init();

  VAddressRange *AllocateVAddressRange(uint64_t size);

  bool ReleaseVAddressRange(VAddressRange *vaddr_range);

  uint32_t GetTotalVAddressRangeNumber() { return total_vaddr_ranges_; }

  uint32_t GetFreeVAddressRangeNumber() { return free_vaddr_ranges_; }

  VAddressRange *FindVAddressRange(uint8_t *address);

 protected:
  uint32_t FindSetBit();

  // A bitmap to mark avail/occupied status of all vaddr_range objects.
  // The value at bit index "i" represents the status of vaddr_range[i].
  // "1": free, "0": unavailable.
  //
  // vaddr_range[i]'s vaddr_range_id is also "i", to facilitate reverse
  // lookup using range_id.
  std::bitset<MAX_VIRTUAL_ADDRESS_RANGES> vaddr_range_bitmap_;

  // A pre-allocated pool of vaddr_ranges.
  VAddressRange* vaddr_range_list_;

  // Size of above array.
  uint32_t total_vaddr_ranges_;

  // Number of vaddr-range objs available to be allocated.
  uint32_t free_vaddr_ranges_;

  // How many vaddr ranges in this group are being used.
  uint32_t inuse_vaddr_ranges_;

  // A balanced search tree to quickly locate a vrange giving
  // arbiartry virtual address.
  AVLTree tree_;
};

bool InitVaddressRangeGroup(VAddressRangeGroup *vgroup);
#endif  // VADDR_RANGE_H_
