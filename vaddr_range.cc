// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "vaddr_range.h"
#include "utils.h"

bool IsValidVAddressRangeId(uint32_t vaddress_range_id) {
  return vaddress_range_id < INVALID_VADDRESS_RANGE_ID;
}

bool VAddressRange::Init(uint64_t size) {
  assert(is_active_ == false);
  size_ = RoundUpToPageSize(size);
  if (size_ < PAGE_SIZE) {
    err("Create vaddr_range size too small: %ld.\n", size);
    return false;
  }
  uint64_t alignment = PAGE_SIZE;
  assert(posix_memalign((void**)&address_, alignment, size_) == 0);
  assert(mprotect(address_, size_, PROT_NONE) == 0);

  number_pages_ = size_ >> PAGE_BITS;
  v2h_map_size_ = number_pages_;
  uint64_t map_byte_size = v2h_map_size_ * sizeof(V2HMapMetadata);
  assert(posix_memalign((void**)&v2h_map_, alignment, map_byte_size) == 0);
  memset(v2h_map_, 0, map_byte_size);
  assert(mlock(v2h_map_, map_byte_size) == 0);

  avl_node_.address = (uint64_t)address_;
  avl_node_.len = size_;
  avl_node_.embedding_object = this;

  has_backing_hdd_file_ = false;
  is_active_ = true;
  dbg("Has created a Vaddr_range:  address = %p, size = %ld\n",
      address_,
      size_);
  return true;
}

bool VAddressRange::Init(uint64_t size,
                         const std::string& hdd_filename,
                         uint64_t hdd_file_offset) {
  assert(is_active_ == false);
  assert(hdd_file_offset % PAGE_SIZE == 0);
  struct stat hdd_file_stat;
  if (stat(hdd_filename.c_str(), &hdd_file_stat) != 0) {
    err("VAddressRange: Fail to stat hdd file %s\n", hdd_filename.c_str());
    perror("Stat error: ");
    return false;
  }
  if (!S_ISREG(hdd_file_stat.st_mode)) {
    err("VAddressRange: hdd file isn't a regular file:  %s\n",
        hdd_filename.c_str());
    return false;
  }
  bool should_truncate = false;
  uint64_t hddfile_old_size = hdd_file_stat.st_size;
  uint64_t hddfile_new_size = 0;
  if (hdd_file_offset + size > hddfile_old_size) {
    should_truncate = true;
    hddfile_new_size = hdd_file_offset + size;
    err("VAddressRange with backing file %s: "
        "virtual-space-size %ld + hdd-file-offset %ld "
        "> file size %ld, will truncate this file to new size %ld\n",
        hdd_filename.c_str(),
        size,
        hdd_file_offset,
        hddfile_old_size,
        hddfile_new_size);
  }
  if (Init(size) == false) {
    err("Cannot init\n");
    return false;
  }
  hdd_file_fd_ = open(hdd_filename.c_str(), O_RDWR | O_DIRECT, 0666);
  assert(hdd_file_fd_ > 0);
  if (should_truncate) {
    assert(ftruncate(hdd_file_fd_, hddfile_new_size) == 0);
  }
  hdd_file_offset_ = hdd_file_offset;
  hdd_filename_ = hdd_filename;
  has_backing_hdd_file_ = true;
  if (hddfile_old_size > hdd_file_offset) {
    uint64_t vaddress_pages_with_old_hddfile_back = std::min<uint64_t>(
        (hddfile_old_size - hdd_file_offset + PAGE_SIZE - 1) / PAGE_SIZE,
        number_pages_);
    // for (uint64_t i = 0; i < v2h_map_size_; ++i) {
    for (uint64_t i = 0; i < vaddress_pages_with_old_hddfile_back; ++i) {
      v2h_map_[i].exist_hdd_file = 1;
    }
    dbg("VAddressRange %d: first %ld pages have init backing in hdd file\n",
        vaddress_range_id(), vaddress_pages_with_old_hddfile_back);
  }

  dbg("VAddressRange %d: opened backing hdd file %s at offset %ld\n",
      vaddress_range_id(), hdd_filename.c_str(), hdd_file_offset_);
  is_active_ = true;
  return is_active_;
}

V2HMapMetadata* VAddressRange::GetV2HMapMetadata(uint64_t address_offset) {
  if (address_offset >= size_) {
    err("address-offset %ld >= size %ld\n", address_offset, size_);
    assert(0);
  }
  return &v2h_map_[address_offset >> PAGE_BITS];
}

void VAddressRange::Release() {
  if (is_active_) {
    free(address_);
    address_ = NULL;
    is_active_ = false;
    if (has_backing_hdd_file_) {
      close(hdd_file_fd_);
      has_backing_hdd_file_ = false;
    }
    uint64_t map_byte_size = v2h_map_size_ * sizeof(V2HMapMetadata);
    munlock(v2h_map_, map_byte_size);
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
    for (uint32_t i = 0; i < total_vaddr_ranges_; ++i) {
      if (vaddr_range_list_[i].is_active()) {
        vaddr_range_list_[i].Release();
      }
    }
    inuse_vaddr_ranges_ = 0;
  }
  DestoryAVL(&tree_);
  if (vaddr_range_list_) {
    delete [] vaddr_range_list_;
  }
}

bool VAddressRangeGroup::Init() {
  // TODO: now only support 8 bits vaddress-range-id [0, 255].
  uint32_t num_vaddr_ranges = MAX_VIRTUAL_ADDRESS_RANGES;
  vaddr_range_list_ = new VAddressRange[num_vaddr_ranges];
  assert(vaddr_range_list_ != NULL);
  for (uint32_t i = 0; i < num_vaddr_ranges; ++i) {
    vaddr_range_list_[i].set_vaddress_range_id(i);
  }
  num_vaddr_ranges = INVALID_VADDRESS_RANGE_ID;  // from 0 to 255.
  total_vaddr_ranges_ = num_vaddr_ranges;
  free_vaddr_ranges_ = num_vaddr_ranges;
  inuse_vaddr_ranges_ = 0;
  // At beginning all vaddr-ranges are free.
  vaddr_range_bitmap_.set();
  return true;
}

VAddressRange* VAddressRangeGroup::AllocateVAddressRange(
    uint64_t size,
    const std::string& hdd_filename,
    uint64_t hdd_file_offset) {
  if (free_vaddr_ranges_ == 0) {
    err("No vaddr-range available.\n");
    return NULL;
  }
  assert(vaddr_range_bitmap_.any() == true);

  for (uint32_t i = 0; i < total_vaddr_ranges_; ++i) {
    if (vaddr_range_bitmap_[i] == 1) {
      VAddressRange *vaddr_range = &vaddr_range_list_[i];
      assert(vaddr_range->Init(size, hdd_filename, hdd_file_offset) == true);
      vaddr_range_bitmap_[i] = 0;
      --free_vaddr_ranges_;
      ++inuse_vaddr_ranges_;
      int ret = InsertNode(&tree_, vaddr_range->GetTreeNode());
      assert(ret == inuse_vaddr_ranges_);
      dbg("Have created a new vaddr-range: size %ld, backed by hdd-file %s "
          "at offset %ld.\nNow have %d active vaddr-ranges.\n",
          size,
          hdd_filename.c_str(),
          hdd_file_offset,
          inuse_vaddr_ranges_);
      return vaddr_range;
    }
  }
  err("All vaddr-ranges used up.\n");
  return NULL;
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
  AVLNode* node = FindNode(&tree_, (uint64_t)vaddr_range->address());
  if (node == NULL) {
    err("Failed to find the vrange.\n");
    return false;
  }
  assert(node == vaddr_range->GetTreeNode());
  assert(inuse_vaddr_ranges_ > 0);
  vaddr_range->Release();

  // Remove the node representing this vaddr-range from BST.
  DeleteNode(&tree_, vaddr_range->GetTreeNode());

  vaddr_range_bitmap_[vaddr_range->vaddress_range_id()] = 1;
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
