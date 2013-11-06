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
  struct value * result = gc_alloc(&value_pool);
  result->leaf = false;
  result->pair.a = a;
  result->pair.b = b;
  return result;
}

struct value * leaf(int x) {
  struct value * result = gc_alloc(&value_pool);
  result->leaf = true;
  result->value = x;
  return result;
}

void inc_all(struct value * value) {
  if (!value) return;
  value->pair.a = leaf(value->pair.a->value+1);
  inc_all(value->pair.b);
}

void print_out(struct value * value) {
  if (!value) return;
  printf("%d \n", value->pair.a->value);
  print_out(value->pair.b);
}

int main() {
  gc_enter();

  gc_enter();
  struct value *test = pair(leaf(1), pair(leaf(2), pair(leaf(3), pair(leaf(4), pair(leaf(5), NULL)))));
  gc_leave();

  gc_root(test);
  
  print_out(test);
  printf("%ld\n", gc());
  gc_enter();
  inc_all(test);
  gc_leave();
  printf("%ld\n", gc());
  print_out(test);
  gc_leave();
  printf("%ld\n", gc());
  return 0;
}
