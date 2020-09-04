#include <stdint.h>

int64_t func42(void) {
  return 42;
}

typedef int64_t (func42_t)(void);
func42_t* funcfunc42(void) {
  return func42;
}

int64_t add(int64_t a, int64_t b) {
  return a + b;
}
