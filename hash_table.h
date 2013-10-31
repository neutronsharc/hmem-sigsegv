#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <string>

#include "debug.h"
#include "hash.h"

// This class implements a bucket-based hash table.
// Objects in this hash table are supposed to contain some fileds:
//   1. "hash_next":  an obj pointer to link objs in a same bucket.
//   2. "hash_key" :  a field to identify the obj. Require this to be a void*.
//   3. "key_size": byte size of the "key". == sizeof(void*)
//
// This class is NOT thread safe.
template <class T>
class HashTable {
 public:
  HashTable()
      : ready_(false), buckets_(NULL), number_buckets_(0), number_objects_(0) {}

  virtual ~HashTable() {
    Release();
  };

  // Prepare internal structs.
  bool Init(const char* name, uint32_t bucket_size, bool pin_in_memory);

  // Free up internal structs.
  bool Release();

  // Insert an obj to hash table. Return true if insert succeeds.
  // False if an obj with same key already exists.
  bool Insert(T* obj);

  T* Lookup(void* key, uint32_t key_size);

  // Remove the obj identified by "key" out of hash table.
  // Return this obj.
  T* Remove(void* key, uint32_t key_size);

  // Find the address of pointer that points to an obj of "key".
  T** FindPrevObjPos(void* key, uint32_t key_size);

  uint32_t GetNumberObjects() { return number_objects_; }

  uint32_t GetNumberBuckets() { return number_buckets_; }

  void ShowStats();

 protected:
  // Indicate if this hash table has its internal struct ready.
  bool ready_;

  // An array of hash-buckets.
  T** buckets_;

  // Number of elements in bucket-array.
  uint32_t number_buckets_;

  // How many objects are stored in this hash table.
  uint32_t number_objects_;

  // name of this hash table.
  std::string name_;

  // Below are stats about the hash table.
  // Max hash collision seen in a bucket.
  uint64_t deepest_collision_;
  uint64_t number_lookups_;
  uint64_t number_inserts_;
  uint64_t number_removes_;
  // How many hash collisions we see.
  uint64_t number_collisions_;
  // How many times we get a lookup hit.
  uint64_t number_hits_;
  // How many times we get a lookup miss.
  uint64_t number_misses_;
};

template <class T>
bool HashTable<T>::Init(const char* name,
                        uint32_t bucket_size,
                        bool pin_in_memory) {
  uint32_t alignment = 4096;
  uint64_t total_byte_size = bucket_size * sizeof(T*);

  assert(posix_memalign((void**)&buckets_, alignment, total_byte_size) == 0);
  // All buckets are empty at beginning.
  memset(buckets_, 0, total_byte_size);
  assert(mlock(buckets_, total_byte_size) == 0);

  number_buckets_ = bucket_size;
  number_objects_ = 0;
  deepest_collision_ = 0;
  number_collisions_ = 0;
  number_lookups_ = 0;
  number_inserts_ = 0;
  number_removes_ = 0;
  number_hits_ = 0;
  number_misses_ = 0;
  ready_ = true;
  name_ = name;
  return true;
}

template <class T>
bool HashTable<T>::Release() {
  if (ready_) {
    uint64_t total_byte_size = number_buckets_ * sizeof(T*);
    munlock(buckets_, total_byte_size);
    free(buckets_);
    ready_ = 0;
    number_buckets_ = 0;
    number_objects_ = 0;
  }
}

template <class T>
bool HashTable<T>::Insert(T* obj) {
  ++number_inserts_;
  if (Lookup((void*)obj->hash_key, obj->key_size) != NULL) {
    err("obj of key=%p already exists in hash table\n", obj->hash_key);
    return false;
  }
  uint32_t hash_value = hash(&obj->hash_key, obj->key_size, 0);
  uint32_t bucket_idx = hash_value % number_buckets_;
  obj->hash_next = buckets_[bucket_idx];
  buckets_[bucket_idx] = obj;
  ++number_objects_;
  return true;
}

template <class T>
T* HashTable<T>::Lookup(void* key, uint32_t key_size) {
  ++number_lookups_;
  uint32_t hash_value = hash(&key, key_size, 0);
  uint32_t bucket_idx = hash_value % number_buckets_;

  T* obj = buckets_[bucket_idx];
  uint32_t depth = 0;
  while (obj) {
    //if (key_size == obj->key_size &&
    //    memcmp(&key, &obj->hash_key, key_size) == key) {
    if (obj->hash_key == key) {
      break;
    }
    ++number_collisions_;
    ++depth;
    obj = obj->hash_next;
  }
  if (depth > deepest_collision_) {
    deepest_collision_ = depth;
  }
  if (obj) {
    ++number_hits_;
  } else {
    ++number_misses_;
  }
  return obj;
}

template <class T>
T** HashTable<T>::FindPrevObjPos(void* key, uint32_t key_size) {
  uint32_t hash_value = hash(&key, key_size, 0);
  uint32_t bucket_idx = hash_value % number_buckets_;
  T** obj = &(buckets_[bucket_idx]);

  while (*obj && (*obj)->hash_key != key) {
    obj = &((*obj)->hash_next);
  }
  return obj;
}

template <class T>
T* HashTable<T>::Remove(void* key, uint32_t key_size) {
  ++number_removes_;
  T** prev_obj_pos = FindPrevObjPos(key, key_size);
  T* target_obj = (*prev_obj_pos);
  if (target_obj == NULL) {
    err("obj key %p not exist.\n", key);
    return NULL;
  }

  T* next_obj = (*prev_obj_pos)->hash_next;
  *prev_obj_pos = next_obj;

  target_obj->hash_next = NULL;
  return target_obj;
}

template <class T>
void HashTable<T>::ShowStats() {
  fprintf(stderr,
          "==================\nHashtable: \"%s\", %d buckets, %d objs, \n"
          "inserts = %ld, lookups = %ld, removes = %ld, hit = %ld, "
          "miss = %ld, deepest-collision = %ld, collisions = %ld\n"
          "=============================\n",
          name_.c_str(),
          number_buckets_,
          number_objects_,
          number_inserts_,
          number_lookups_,
          number_removes_,
          number_hits_,
          number_misses_,
          deepest_collision_,
          number_collisions_);
}


#endif  // HASH_TABLE_H_
