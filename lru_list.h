// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef LRU_LIST_H_
#define LRU_LIST_H_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "avl.h"
#include "hybrid_memory_const.h"


// This class implements a LRU-style list.
// Most recently-accessed objs are promoted to head, as a result
// less-recently-accessed objs gradually sink to tail.
//
// Internally the LRU-list is a double-linked list.
// "head" points to the Most-Recent-obj, head->lru_prev == NULL;
// "tail" points the Least-Recent-obj, and tail->lru_next == NULL;
//
// This class is NOT thread safe.
template <class T>
class LRUList {
 public:
  LRUList() : head_(NULL), tail_(NULL), number_objects_(0) {}
  virtual ~LRUList() {}

  // Insert the obj as a most-recent one.
  void Link(T* x);

  // Take the obj out of LRU list.
  void Unlink(T* x);

  // Move the obj to the most-recent end of LRU list.
  void Update(T* x);

  T* head() { return head_; }

  T* tail() { return tail_; }

  uint64_t GetNumberObjects() { return number_objects_; }

 protected:
  // Most-Recent obj.
  T* head_;

  // Least-Recent obj.
  T* tail_;

  // Number of objects currently in the LRU cache.
  uint64_t number_objects_;
};

template <class T>
void LRUList<T>::Link(T* x) {
  if (head_) {
    // x is the new "most-recent" one.
    x->lru_prev = NULL;
    x->lru_next = head_;
    head_->lru_prev = x;
    head_ = x;
  } else {
    assert(head_ == NULL && tail_ == NULL);
    head_ = x;
    tail_ = x;
    x->lru_next = NULL;
    x->lru_prev = NULL;
  }
  ++number_objects_;
}

template <class T>
void LRUList<T>::Unlink(T* x) {
  assert(number_objects_ > 0);
  T* prev = x->lru_prev;
  T* next = x->lru_next;
  if (prev && next) {
    // this obj in the middle of list.
    prev->lru_next = next;
    next->lru_prev = prev;
  } else if (prev && !next) {
    // List > 1 and this obj is at last: the least-recent one.
    prev->lru_next = NULL;
    tail_ = prev;
  } else if (!prev && next) {
    // List > 1 and this obj is at head: the most-recent one.
    next->lru_prev = NULL;
    head_ = next;
  } else {
    // List size = 1.
    head_ = NULL;
    tail_ = NULL;
  }
  --number_objects_;
}

template <class T>
void LRUList<T>::Update(T* x) {
  Unlink(x);
  Link(x);
}

#endif  // LRU_LIST_H_
