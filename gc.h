#ifndef gc_h
#define gc_h

#include <sys/types.h>
#include <stdlib.h>
#include <stdbool.h>

typedef void (*tracer_t)(void * ptr);

typedef struct pool {
  size_t size;
  tracer_t tracer;
  struct node * free;
  struct page * pages;
  struct pool * next;
} pool_t;

#define ALIGN(size)                           \
  ((size) < sizeof(struct node *) ? sizeof(struct node *) : \
   (size) < 8 ? 8 :                           \
   ALIGN_TO(16, size))
#define ALIGN_TO(size, align) ((((size)-1) + (align)) & ~(align-1))
#define NEW_POOL(type, trace)                 \
  ((struct pool) {                            \
    ALIGN(sizeof(type)),                      \
    (tracer_t) (trace),                       \
    NULL, NULL                                \
      })

void * gc_alloc(pool_t * pool);
void gc_root(void * ptr);
void gc_enter(void);
void gc_leave(void);
void gc_trace(void * ptr);
size_t gc(void);

#endif
