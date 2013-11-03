#include <assert.h>
#include <stdio.h>
#include "bitmap.h"

#include <iostream>

static void TestBitmap() {
  Bitmap<1024> *bitmap = new Bitmap<1024>[10];

  Bitmap<256> b2;
  printf("10 bitmap of 1024bits, size = %ld\n", sizeof(b2));
  b2.set_all();
  assert(b2.number_of_set_bits() == 256);
  b2.clear(3);
  b2.clear(8);
  assert(b2.number_of_set_bits() == 254);
  assert(b2.number_of_clear_bits() == 2);
  std::cout << b2.to_string() << std::endl;
  printf("ffs is %ld\n", b2.ffs());
  assert(b2.get(1) == 1);
  assert(b2.get(3) == 0);
  assert(b2.get(8) == 0);
  assert(b2.get(256) == 1);

  printf("now clear the bitmap\n");
  b2.clear_all();
  b2.set(256);
  b2.set(220);
  b2.set(210);
  assert(b2.number_of_set_bits() == 3);
  assert(b2.number_of_clear_bits() == 253);
  printf("ffs is %ld\n", b2.ffs());
  std::cout << b2.to_string() << std::endl;
  assert(b2.get(1) == 0);
  assert(b2.get(2) == 0);
  assert(b2.get(210) == 1);
  assert(b2.get(220) == 1);
  assert(b2.get(255) == 0);
  assert(b2.get(256) == 1);

  for (uint32_t i = 0; i < 10; i++) {
    printf("bitmap[%d] addr = %p\n", i, &bitmap[i]);
  }

}

int main(int argc, char **argv) {
  TestBitmap();
  printf("\nPASS\n");
  return 0;
}
