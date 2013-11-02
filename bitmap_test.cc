#include <stdio.h>
#include "bitmap.h"

#include <iostream>

static void TestBitmap() {
  BitMap<1024> *bitmap = new BitMap<1024>[10];

  BitMap<256> b2;
  printf("10 bitmap of 1024bits, size = %ld\n", sizeof(b2));
  b2.set_all();
  b2.clear(3);
  b2.clear(8);
  std::cout << b2.to_string() << std::endl;
  printf("ffs is %ld\n", b2.ffs());
  assert(b2.get(3) == 0);
  assert(b2.get(8) == 0);

  printf("now clear the bitmap\n");
  b2.clear_all();
  b2.set(256);
  b2.set(220);
  b2.set(210);
  printf("ffs is %ld\n", b2.ffs());
  std::cout << b2.to_string() << std::endl;
  assert(b2.get(210) == 1);

  for (uint32_t i = 0; i < 10; i++) {
    printf("bitmap[%d] addr = %p\n", i, &bitmap[i]);
  }

}

int main(int argc, char **argv) {
  TestBitmap();
  printf("\nPASS\n");
  return 0;
}
