#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "debug.h"
#include "vaddr_range.h"
#include "utils.h"

VAddressRange::VAddressRange()
    : is_active_(false),
      address_(NULL),
      size_(0),
      number_pages_(0),
      v2h_map_(NULL),
      v2h_map_size_(0) {}

VAddressRange::~VAddressRange() {
  Release();
}

bool VAddressRange::Init(uint64_t size) {
  assert(is_active_ == false);
  uint64_t alignment = PAGE_SIZE;
  size_ = RoundUpToPageSize(size);
  if (size_ < PAGE_SIZE) {
    err("Create vaddr_range size too small: %ld.\n", size);
    return false;
  }
  assert(posix_memalign((void**)&address_, alignment, size_) == 0);
  assert(mprotect(address_, size_, PROT_NONE) == 0);

  number_pages_ = size_ >> PAGE_BITS;
  v2h_map_size_ = number_pages_ * sizeof(V2HMapMetadata);
  assert(posix_memalign((void**)&v2h_map_, alignment, v2h_map_size_) == 0);
  memset(v2h_map_, 0, v2h_map_size_);
  assert(mlock(v2h_map_, v2h_map_size_) == 0);

  avl_node_.address = (uint64_t)address_;
  avl_node_.len = size_;
  avl_node_.embedding_object = this;

  is_active_ = true;
  dbg("Has created a Vaddr_range:  address = %p, size = %ld\n",
      address_,
      size_);
  return true;
}

V2HMapMetadata* VAddressRange::GetV2HMapMetadata(uint64_t address_offset) {
  assert(address_offset < size_);
  return &v2h_map_[address_offset >> PAGE_BITS];
}

void VAddressRange::Release() {
  if (is_active_) {
    free(address_);
    address_ = NULL;
    is_active_ = false;
    munlock(v2h_map_, v2h_map_size_ * sizeof(V2HMapMetadata));
    free(v2h_map_);
    v2h_map_ = NULL;
  }
}

VAddressRangeGroup::VAddressRangeGroup()
    : vaddr_range_list_(NULL),
      total_vaddr_ranges_(0),
      free_vaddr_ranges_(0),
      inuse_vaddr_ranges_(0) {
  InitAVL(&tree_);
}

VAddressRangeGroup::~VAddressRangeGroup() {
  if (inuse_vaddr_ranges_ > 0) {
    // TODO:  release vaddr-ranges if there are still in-use vaddr-ranges.
    err("Lingering vaddr_ranges exist when vgroup is deleted.\n");
    inuse_vaddr_ranges_ = 0;
  }
  DestoryAVL(&tree_);
  if (vaddr_range_list_) {
    //delete vaddr_range_list_;
  }
}

bool VAddressRangeGroup::Init() {
  uint32_t num_vaddr_ranges = MAX_VIRTUAL_ADDRESS_RANGES;
  vaddr_range_list_ = new VAddressRange[num_vaddr_ranges];
  assert(vaddr_range_list_ != NULL);
  for (uint32_t i = 0; i < num_vaddr_ranges; ++i) {
    vaddr_range_list_[i].vaddr_range_id_ = i;
  }
  total_vaddr_ranges_ = num_vaddr_ranges;
  free_vaddr_ranges_ = num_vaddr_ranges;
  inuse_vaddr_ranges_ = 0;
  // At beginning all vaddr-ranges are free.
  vaddr_range_bitmap_.set();
  return true;
}

VAddressRange* VAddressRangeGroup::AllocateVAddressRange(uint64_t size) {
  if (free_vaddr_ranges_ == 0) {
    err("No vaddr-range available.\n");
    return NULL;
  }

  assert(vaddr_range_bitmap_.any() == true);

  for (uint32_t i = 0; i < total_vaddr_ranges_; ++i) {
    if (vaddr_range_bitmap_[i] == 1) {
      VAddressRange *vaddr_range = &vaddr_range_list_[i];
      assert(vaddr_range->Init(size) == true);
      // TODO: each vaddr-range 1-on-1 maps to a hdd-file. Open the
      // hdd file and records the filename / offset.
      vaddr_range_bitmap_[i] = 0;
      --free_vaddr_ranges_;
      ++inuse_vaddr_ranges_;
      int ret = InsertNode(&tree_, vaddr_range->GetTreeNode());
      assert(ret == inuse_vaddr_ranges_);
      dbg("Have inserted a new range to BST. Now have %d ranges\n",
          inuse_vaddr_ranges_);
      return vaddr_range;
    }
  }
  err("All vaddr-ranges used up.\n");
  return NULL;
}

bool VAddressRangeGroup::ReleaseVAddressRange(VAddressRange* vaddr_range) {
  AVLNode* node = FindNode(&tree_, (uint64_t)vaddr_range->GetAddress());
  if (node == NULL) {
    err("Failed to find the vrange.\n");
    return false;
  }
  assert(node == vaddr_range->GetTreeNode());
  assert(inuse_vaddr_ranges_ > 0);
  vaddr_range->Release();

  // Remove the node representing this vaddr-range from BST.
  DeleteNode(&tree_, vaddr_range->GetTreeNode());

  vaddr_range_bitmap_[vaddr_range->vaddr_range_id_] = 1;
  ++free_vaddr_ranges_;
  --inuse_vaddr_ranges_;

  dbg("Have deleted a vrange. Now have %d ranges\n",
      inuse_vaddr_ranges_);
  return true;
}

VAddressRange* VAddressRangeGroup::FindVAddressRange(uint8_t* address) {
  AVLNode *node = FindNode(&tree_, (uint64_t)address);
  if (node == NULL) {
    err("Cannot find VAddress_range enclosing addr: %p\n", address);
    return NULL;
  }
  return (VAddressRange*)node->embedding_object;
}
