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
  size_t nfree, npages;
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
    NULL, NULL, 0, 0                          \
      })

extern int gc_nroots;
extern int gc_maxroots;
extern void ** gc_roots;
void gc_expand_roots(void);
static inline void gc_root(void * ptr) {
  if (!ptr) return;
  if (gc_nroots == gc_maxroots) gc_expand_roots();
  gc_roots[gc_nroots++] = ptr;
}

struct node {
  struct node * next;
};

void * gc_alloc_slow(pool_t * pool);
static inline void * gc_alloc(pool_t * pool) {
  struct node * free = pool->free;
  if (free) {
    pool->free = free->next;
    pool->nfree--;
    gc_root(free);
    return free;
  } else return gc_alloc_slow(pool);
}

#define GC_PIN(ptr) (gc_root(ptr), (ptr))
static inline void gc_enter(void) {
  if (gc_nroots == gc_maxroots) gc_expand_roots();
  gc_roots[gc_nroots++] = NULL;
}

static inline void gc_leave(void) {
  while (gc_nroots > 0 && gc_roots[gc_nroots-1])
    gc_nroots--;
  if (gc_nroots > 0)
    gc_nroots--;
}

void gc_trace(void * ptr);
#define GC_ENTER(...) do { \
  void *gc_objs[] = {__VA_ARGS__}; \
  int i; \
  gc_enter(); \
  for (i = 0; i < sizeof gc_objs / sizeof gc_objs[0]; i++) \
    gc_root(gc_objs[i]); } while(0)
#define GC_LEAVE(...) do { \
  void *gc_objs[] = {__VA_ARGS__}; \
  int i; \
  gc_leave(); \
  for (i = 0; i < sizeof gc_objs / sizeof gc_objs[0]; i++) \
    gc_root(gc_objs[i]); } while(0)
#define GC_RETURN(x) do { GC_LEAVE(x); return (x); } while(0)
#define GC_RESET(...) do { GC_LEAVE(); GC_ENTER(__VA_ARGS__); } while(0)
size_t gc(void);
void gc_stats(void);

#endif
