// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef FREE_LIST_H_
#define FREE_LIST_H_

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include <string>

#include "debug.h"
#include "hybrid_memory_const.h"
#include "utils.h"

// This class implements a free-list of objs.
// Each class T is supposed to contain following fields:
//   "void *data":  the payload field of the obj.
//
// This class is NOT thread safe.
template <class T>
class FreeList {
 public:
  FreeList()
      : ready_(false),
        all_objects_(NULL),
        list_(NULL),
        available_objects_(0),
        total_objects_(0) {}

  ~FreeList() { Release(); }

  // Init the free-list internal structs.
  bool Init(const std::string& name,
            uint64_t total_objects,
            uint64_t object_datasize,
            bool pin_memory);

  // Free up internal resources before exit.
  bool Release();

  T* New();

  void Free(T* x);

  void ShowStats();

  // Get number of available objects.
  uint64_t AvailObjects() { return available_objects_; }

  uint64_t TotalObjects() { return total_objects_; }

 protected:
  // Indicate if this free-list has been initialized.
  bool ready_;

  // A contiguous memory area to store all objects.
  T* all_objects_;

  // An array to store pointers to available objects.
  T** list_;

  // If each object has a "data" field, we pre-allocate a memory-space
  // and assign a piece of mem from this space to each object.
  void* objects_data_;

  // The list array has this many objects.
  uint64_t total_objects_;

  // The free-list contains this many avail objs.
  uint64_t available_objects_;

  // Payload data size at each object.
  uint64_t object_datasize_;

  // If we should pin the free-list memory.
  bool pin_memory_;

  std::string name_;
};

template <class T>
bool FreeList<T>::Init(const std::string& name,
                       uint64_t total_objects,
                       uint64_t object_datasize,
                       bool pin_memory) {
  assert(ready_ == false);
  total_objects_ = total_objects;
  pin_memory_ = pin_memory;
  object_datasize_ = RoundUpToPageSize(object_datasize);
  uint64_t total_objects_size = total_objects_ * sizeof(T);
  uint64_t total_list_size = total_objects_ * sizeof(T*);
  uint64_t total_objects_datasize = total_objects_ * object_datasize_;
  uint32_t alignment = PAGE_SIZE;
  assert(posix_memalign((void**)&all_objects_, alignment, total_objects_size) ==
         0);
  assert(posix_memalign((void**)&list_, alignment, total_list_size) == 0);
  if (object_datasize_ > 0) {
    dbg("freelist %s: pre-allocate data area %ld for %ld objs\n",
        name.c_str(),
        total_objects_datasize,
        total_objects);
    assert(posix_memalign((void**)&objects_data_, alignment, total_objects_datasize) ==
           0);
  }
  if (pin_memory_) {
    assert(mlock(all_objects_, total_objects_size) == 0);
    assert(mlock(list_, total_list_size) == 0);
    assert(mlock(objects_data_, total_objects_datasize) == 0);
  }

  uint64_t data = (uint64_t)objects_data_;
  for (uint32_t i = 0; i < total_objects_; ++i) {
    all_objects_[i].data = (void*)data;
    if (object_datasize_ > 0) {
      data += object_datasize_;
    }
    list_[i] = &all_objects_[i];
  }
  available_objects_ = total_objects_;
  name_ = name;
  dbg("Have inited freelist \"%s\": %ld objs, obj-datasize %ld, "
      "pin-memory=%d\n",
      name.c_str(),
      total_objects_,
      object_datasize_,
      pin_memory);
  ready_ = true;
  return true;
}

template <class T>
bool FreeList<T>::Release() {
  if (ready_) {
    dbg("Release free-list \"%s\"...\n", name_.c_str());
    if (pin_memory_) {
      munlock(all_objects_, total_objects_ * sizeof(T));
      munlock(list_, total_objects_ * sizeof(T*));
      if (object_datasize_ > 0) {
        munlock(objects_data_, total_objects_ * object_datasize_);
      }
    }
    free(all_objects_);
    free(list_);
    if (object_datasize_ > 0) {
      free(objects_data_);
    }
    total_objects_ = 0;
    ready_ = false;
  }
  return true;
}

template <class T>
T* FreeList<T>::New() {
  if (available_objects_ == 0) {
    return NULL;
  }
  return list_[--available_objects_];
}

template <class T>
void FreeList<T>::Free(T* x) {
  list_[available_objects_++] = x;
}

template <class T>
void FreeList<T>::ShowStats() {
  fprintf(stderr,
          "Freelist \"%s\", total %ld objs, %ld avail-objs, objsize = %ld, "
          "obj-datasize=%ld\n",
          name_.c_str(),
          total_objects_,
          available_objects_,
          sizeof(T),
          object_datasize_);
  fflush(stderr);
}

#endif  // FREE_LIST_H_
