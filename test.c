#include "gc.h"
#include <stdio.h>

struct value {
  bool leaf;
  union {
    struct { struct value * a; struct value * b; } pair;
    int value;
  };
};

void trace_value(struct value * value) {
  if(!value->leaf) {
    gc_trace(value->pair.a);
    gc_trace(value->pair.b);
  }
}

pool_t value_pool = NEW_POOL(struct value, trace_value);

struct value * pair(struct value * a, struct value * b) {
  GC_ENTER(a, b);
  struct value * result = gc_alloc(&value_pool);
  result->leaf = false;
  result->pair.a = a;
  result->pair.b = b;
  GC_RETURN(result);
}

struct value * leaf(int x) {
  GC_ENTER();
  struct value * result = gc_alloc(&value_pool);
  result->leaf = true;
  result->value = x;
  GC_RETURN(result);
}

void inc_all(struct value * value) {
  GC_ENTER();
  while(value) {
    value->pair.a = leaf(value->pair.a->value+1);
    value = value->pair.b;
  }
  GC_LEAVE();
}

void print_out(struct value * value) {
  GC_ENTER();
  while(value) {
    printf("%d ", value->pair.a->value);
    value = value->pair.b;
  }
  printf("\n");
  GC_LEAVE();
}

void big_test() {
  GC_ENTER();
  struct value *test = NULL;
  int i;
  for (i = 0; i < 1000000; i++) {
    test = pair(leaf(i), test);
    GC_RESET(test);
  }
  printf("%ld bytes freed\n", gc());
  inc_all(test);
  printf("%ld bytes freed\n", gc());

  GC_LEAVE();
  printf("%ld bytes freed\n", gc());
}

int main() {
  GC_ENTER();

  struct value *test = pair(leaf(1), pair(leaf(2), pair(leaf(3), pair(leaf(4), pair(leaf(5), NULL)))));
  GC_RESET(test);
  printf("%ld bytes freed\n", gc());
  
  print_out(test);
  printf("%ld bytes freed\n", gc());
  inc_all(test);
  printf("%ld bytes freed\n", gc());
  print_out(test);
  GC_LEAVE();
  printf("%ld bytes freed\n", gc());
  big_test();
  return 0;
}
