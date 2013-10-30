#ifndef FREE_LIST_H_
#define FREE_LIST_H_

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"

// This class is NOT thread safe.
template <class T>
class FreeList {
 public:
  FreeList(uint32_t total_objects);
  ~FreeList();

  T* New();
  void Free(T* x);

  void Dump();

  // Get number of available objects.
  uint32_t AvailObjects() { return available_objects_; }

 protected:
  // A contiguous memory area to store all objects.
  T* all_objects_;

  // An array to store pointers to available objects.
  T** list_;

  // The free-list contains this many avail objs.
  uint32_t available_objects_;

  // The list array has this many total objects.
  uint32_t total_objects_;
  uint32_t object_size_;
};

template <class T>
FreeList<T>::FreeList(uint32_t total_objects)
    : total_objects_(total_objects) {
  object_size_ = sizeof(T);
  all_objects_ = (T*)malloc(total_objects_ * object_size_);
  list_ = (T**)malloc(total_objects_ * sizeof(T*));
  for (uint32_t i = 0; i < total_objects_; ++i) {
    list_[i] = &all_objects_[i];
  }
  available_objects_ = total_objects_;
}

template <class T>
FreeList<T>::~FreeList() {
  dbg("Release free-list...\n");
  free(all_objects_);
  free(list_);
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
void FreeList<T>::Dump() {
  fprintf(stderr, "Total %d objs, %d avail-objs, obj-size = %d\n",
          total_objects_, available_objects_, object_size_);

}

#endif  // FREE_LIST_H_
