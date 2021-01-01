#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

int64_t* alloc4(int64_t a, int64_t b, int64_t c, int64_t d) {
  int64_t* arr = malloc(4 * sizeof(int64_t));
  arr[0] = a;
  arr[1] = b;
  arr[2] = c;
  arr[3] = d;
  return arr;
}

int64_t variadic_sum(int64_t argc, ...) {
  int64_t sum = 0;

  va_list l;
  va_start(l, argc);
  for (int64_t i = 0; i < argc; ++i) {
    sum += va_arg(l, int64_t);
  }
  va_end(l);

  return sum;
}

void print_int64(int64_t v) {
  printf("%" PRIi64, v);
}

void print_string(const char* s) {
  printf("%s", s);
}
