#include <stdio.h>
#include <stdlib.h>
#include "hash_table.h"

struct TestObject {
  TestObject *hash_next;
  void *hash_key;
  uint32_t key_size;

  uint64_t data;
};

static void TestHashTable() {
  HashTable<TestObject> hash_table;
  bool pin_in_memory = true;
  uint32_t buckets = 16;
  assert(hash_table.Init("test table", buckets, pin_in_memory) == true);
  assert(hash_table.GetNumberObjects() == 0);
  assert(hash_table.GetNumberBuckets() == buckets);
  hash_table.ShowStats();

  uint32_t number_objs = 32;
  TestObject *objs = new TestObject[number_objs];

  for (uint32_t i = 0; i < number_objs; ++i) {
    objs[i].hash_key = malloc(512);
    objs[i].key_size = sizeof(void*);
    printf("obj %d: key = %p\n", i, objs[i].hash_key);
  }

  for (uint32_t i = 0; i < number_objs; ++i) {
    assert(hash_table.Insert(objs + i) == true);
  }
  assert(hash_table.GetNumberObjects() == number_objs);
  for (uint32_t i = 0; i < number_objs; ++i) {
    assert(hash_table.Lookup(objs[i].hash_key, sizeof(void*)) == objs + i);
  }
  hash_table.ShowStats();

  assert(hash_table.Insert(objs + 2) == false);

  assert(hash_table.Lookup(objs[3].hash_key, sizeof(void*)) == &objs[3]);

  assert(hash_table.Remove(objs[6].hash_key, sizeof(void*)) == (objs + 6));
  assert(hash_table.Lookup(objs[6].hash_key, sizeof(void*)) == NULL);
  assert(hash_table.Remove(objs[6].hash_key, sizeof(void*)) == NULL);

  for (uint32_t i = 0; i < number_objs; ++i) {
    hash_table.Remove(objs[i].hash_key, sizeof(void*));
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
