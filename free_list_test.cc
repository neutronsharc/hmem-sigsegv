// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include <stdio.h>
#include "free_list.h"

struct MyObj {
  uint64_t id;

  MyObj *next;
  MyObj *prev;

  void *data;
};

static void TestFreeList() {
  FreeList<MyObj> list;
  uint32_t number_objects = 1000;
  uint32_t payload_size = 4096;
  bool page_align = true;
  bool pin_memory = true;
  assert(list.Init("test list", number_objects, payload_size, page_align,
                   pin_memory) == true);
  assert(list.AvailObjects() == number_objects);
  assert(list.TotalObjects() == number_objects);
  list.ShowStats();

  MyObj *objs[number_objects];
  for (uint32_t i = 0; i <number_objects; i++) {
    objs[i] = list.New();
  }
  list.ShowStats();
  assert(list.AvailObjects() == 0);

  list.Free(objs[0]);
  assert(list.AvailObjects() == 1);

  MyObj *one_obj = list.New();
  assert(one_obj == objs[0]);

  for (uint32_t i = 0; i < number_objects; i++) {
    list.Free(objs[i]);
  }
  assert(list.AvailObjects() == number_objects);
  list.ShowStats();
}

int main(int argc, char **argv) {
  TestFreeList();
  printf("PASS.\n");
  return 0;
}
