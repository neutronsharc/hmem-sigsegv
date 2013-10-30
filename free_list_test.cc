#include <stdio.h>
#include "free_list.h"

typedef struct MyObj_s {
  uint64_t id;
  char name[128];
} MyObj;

static void TestFreeList() {
  FreeList<MyObj> list(100);
  list.Dump();

  MyObj *objs[100];
  for (uint32_t i = 0; i < 100; i++) {
    objs[i] = list.New();
  }
  list.Dump();
  assert(list.AvailObjects() == 0);

  list.Free(objs[0]);
  assert(list.AvailObjects() == 1);

  MyObj *one_obj = list.New();
  assert(one_obj == objs[0]);

  for (uint32_t i = 0; i < 100; i++) {
    list.Free(objs[i]);
  }
  assert(list.AvailObjects() == 100);
  list.Dump();
}

int main(int argc, char **argv) {
  TestFreeList();
  return 0;
}
