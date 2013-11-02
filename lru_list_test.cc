#include <bitset>   
#include <stdio.h>
#include "lru_list.h"

struct Item {
  Item *lru_prev;
  Item *lru_next;

  int data;
};

void DumpLRU(LRUList<Item>* lru_list) {
  Item* item = lru_list->head();
  printf("LRU list (most-recent head first):\n");
  while (item) {
    printf("%d ", item->data);
    item = item->lru_next;
  }
  printf("\n");
}

static void TestLruList() {
  uint32_t number_items = 10;
  Item *items = new Item[number_items];

  for (uint32_t i = 0; i < number_items; ++i) {
    items[i].data = i;
  }

  LRUList<Item> lru_list;
  for (uint32_t i = 0; i < number_items; ++i) {
    lru_list.Link(items + i);
  }
  assert(lru_list.GetNumberObjects() == number_items);
  DumpLRU(&lru_list);

  printf("will remove 5...\n");
  Item* it = &items[5];
  lru_list.Unlink(it);
  assert(lru_list.GetNumberObjects() == number_items - 1);
  DumpLRU(&lru_list);

  printf("will remove 2...\n");
  it = &items[2];
  lru_list.Unlink(it);
  assert(lru_list.GetNumberObjects() == number_items - 2);
  DumpLRU(&lru_list);

  printf("will insert 5...\n");
  it = &items[5];
  lru_list.Link(it);
  assert(lru_list.GetNumberObjects() == number_items - 1);
  DumpLRU(&lru_list);

  printf("will update 3...\n");
  it = &items[3];
  lru_list.Update(it);
  assert(lru_list.GetNumberObjects() == number_items - 1);
  DumpLRU(&lru_list);
  delete items;
}

int main(int argc, char **argv) {
  std::bitset<1024> bs;
  printf("bitset<1024> size = %ld, <28> = %ld, <1048576>=%ld\n",
         sizeof(bs),
         sizeof(std::bitset<28>), sizeof(std::bitset<1048576>));
  return 0;
  TestLruList();
  printf("PASS\n");
  return 0;
}
