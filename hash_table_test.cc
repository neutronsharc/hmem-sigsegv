// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

#include "hash_table.h"

struct TestObject {
  TestObject *hash_next;
  void *hash_key;
  //uint32_t key_size;

  uint64_t data;
};

static void TestHashTable() {
  HashTable<TestObject> hash_table;
  bool pin_in_memory = true;
  uint32_t buckets = 1000000;
  assert(hash_table.Init("test table", buckets, pin_in_memory) == true);
  assert(hash_table.GetNumberObjects() == 0);
  assert(hash_table.GetNumberBuckets() == buckets);
  hash_table.ShowStats();

  uint32_t number_objs = buckets * 2;
  TestObject *objs = new TestObject[number_objs];

  for (uint32_t i = 0; i < number_objs; ++i) {
    objs[i].hash_key = malloc(512);
    //objs[i].key_size = sizeof(void*);
  }

  for (uint32_t i = 0; i < number_objs; ++i) {
    assert(hash_table.Insert(objs + i, sizeof(void*)) == true);
  }
  assert(hash_table.GetNumberObjects() == number_objs);
  struct timespec tstart, tend;
  clock_gettime(CLOCK_REALTIME, &tstart);
  for (uint32_t i = 0; i < number_objs; ++i) {
    assert(hash_table.Lookup(objs[i].hash_key, sizeof(void*)) == objs + i);
  }
  clock_gettime(CLOCK_REALTIME, &tend);
  hash_table.ShowStats();
  int64_t total_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                     (tend.tv_nsec - tstart.tv_nsec);
  printf("%d lookup, cost %ld ns, %f ns/lookup\n",
         number_objs,
         total_ns,
         (total_ns + 0.0) / number_objs);

  assert(hash_table.Insert(objs + 2, sizeof(void*)) == false);

  assert(hash_table.Lookup(objs[3].hash_key, sizeof(void*)) == &objs[3]);

  assert(hash_table.Remove(objs[6].hash_key, sizeof(void*)) == (objs + 6));
  assert(hash_table.Lookup(objs[6].hash_key, sizeof(void*)) == NULL);
  assert(hash_table.Remove(objs[6].hash_key, sizeof(void*)) == NULL);

  for (uint32_t i = 0; i < number_objs; ++i) {
    hash_table.Remove(objs[i].hash_key, sizeof(void*));
    assert(objs[i].hash_next == NULL);
    free(objs[i].hash_key);
  }
  hash_table.ShowStats();
  delete objs;
}

int main(int argc, char **argv) {
  TestHashTable();
  printf("Test passed.\n");
  return 0;
}
