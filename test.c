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
  struct value *test = pair(NULL, NULL); gc_push_root(test);
  test->pair.a = leaf(1);
  struct value *p1 = pair(NULL, NULL);
  test->pair.b = p1;
  p1->pair.a = leaf(2);
  struct value *p2 = pair(NULL, NULL);
  p1->pair.b = p2;
  p2->pair.a = leaf(3);
  struct value *p3 = pair(NULL, NULL);
  p2->pair.b = p3;
  p3->pair.a = leaf(4);
  struct value *p4 = pair(NULL, NULL);
  p3->pair.b = p4;
  p4->pair.a = leaf(5);
  
  print_out(test);
  printf("%ld\n", gc());
  inc_all(test);
  printf("%ld\n", gc());
  print_out(test);
  gc_pop_root(test);
  printf("%ld\n", gc());
  return 0;
}
