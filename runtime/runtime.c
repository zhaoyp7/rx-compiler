typedef int int32_t;
typedef unsigned int uint32_t;

extern int printf(const char *format, ...);
extern int scanf(const char *format, ...);
extern void *malloc(unsigned int size);

int32_t get_i32(void) {
  int32_t value = 0;
  scanf("%d", &value);
  return value;
}

void print_i32(int32_t value) {
  printf("%d", value);
}

void println_i32(int32_t value) {
  printf("%d\n", value);
}

void *__rx_alloc(uint32_t size, uint32_t align) {
  (void)align;
  return malloc(size);
}
