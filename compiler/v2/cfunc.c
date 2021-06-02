#include <inttypes.h>

int64_t func42(void) {
  return 42;
}

typedef int64_t (func42_t)(void);
func42_t* funcfunc42(void) {
  return func42;
}
