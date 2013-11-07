#include "gc.h"
#include <stdio.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

static void fatal(const char * msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  abort();
}

int gc_oldnroots = 0;
int gc_nroots = 0;
int gc_maxroots = 0;
static size_t roots_size = 0;
void ** gc_roots = 0;

#define PAGE_SIZE 4096

static void * page_alloc(size_t size) {
  void * ptr = mmap(0, size*2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (ptr == MAP_FAILED) return 0;
  void * aligned = (void *)ALIGN_TO((size_t)ptr, size);
  munmap(ptr, aligned-ptr);
  munmap(aligned+size, ptr+size-aligned);
  return aligned;
}

void gc_expand_roots() {
  if (!gc_roots) {
    gc_roots = page_alloc(PAGE_SIZE);
    if (!gc_roots) fatal("no memory for roots");
    roots_size = PAGE_SIZE;
    gc_maxroots = PAGE_SIZE / sizeof(void *);
  }
  void ** new_roots = page_alloc(roots_size * 2);
  if (!new_roots) fatal("out of memory for roots");
  memcpy(new_roots, gc_roots, gc_nroots * sizeof(void *));
  munmap(gc_roots, roots_size);
  gc_roots = new_roots;
  roots_size *= 2;
  gc_maxroots = roots_size / sizeof(void *);
}

#define BITMAP_SIZE (PAGE_SIZE/8)
#define DATA_SIZE (PAGE_SIZE - BITMAP_SIZE - sizeof(struct page *) - sizeof(struct pool *))

struct page {
  char data[DATA_SIZE];
  unsigned char bitmap[BITMAP_SIZE];
  struct pool * pool;
  struct page * next;
};

#define LO(index) ((index)/8)
#define HI(index) (1 << ((index) % 8))

static inline bool bit_test(unsigned char * bitmap, unsigned index) {
  return bitmap[LO(index)] & HI(index);
}

static inline void bit_set(unsigned char * bitmap, unsigned index) {
  bitmap[LO(index)] |= HI(index);
}

static inline void bit_clear(unsigned char * bitmap, unsigned index) {
  bitmap[LO(index)] &= ~HI(index);
}

static size_t gc_reclaim_page(pool_t * pool, struct page * page) {
  size_t ofs = 0, i = 0, result = 0;
  struct node * next = pool->free;
  while (ofs + pool->size <= DATA_SIZE) {
    if (!bit_test(page->bitmap, i)) {
      struct node * node = (struct node *)(&page->data[ofs]);
      node->next = next;
      next = node;
      result++;
    }
    ofs += pool->size;
    i++;
  }
  pool->free = next;
  pool->nfree += result;
  return result;
}

static void gc_new_page(pool_t * pool) {
  assert(sizeof(struct page) == PAGE_SIZE);
  struct page * page = page_alloc(PAGE_SIZE);
  if (!page) fatal("mmap failed");
  /* page->bitmap is already zeroed */
  page->pool = pool;
  page->next = pool->pages;
  pool->pages = page;
  pool->npages++;
  gc_reclaim_page(pool, page);
}

static pool_t * pools;

static void gc_reserve(pool_t * pool) {
  if (pool->free) return;
  gc();
  if (pool->nfree <= 0.3 * pool->npages * (DATA_SIZE / pool->size)) {
    size_t npages = pool->npages;
    size_t i;
    for (i = 0; i < npages+1; i++)
      gc_new_page(pool);
  }
}

static void gc_init(pool_t * pool) {
  if (pool->next || pool == pools) return;
  pool->next = pools;
  pools = pool;
}

void * gc_alloc_slow(pool_t * pool) {
  gc_init(pool);
  gc_reserve(pool);
  struct node * free = pool->free;
  if (!free) return NULL;
  pool->free = free->next;
  pool->nfree--;
  gc_root(free);
  return free;
}

static size_t gc_trim(pool_t * pool) {
  size_t nfree = pool->nfree;
  pool->nfree = 0;
  pool->free = NULL;
  pool->next = NULL;

  size_t result = 0;
  struct page * page = pool->pages;
  while (page) {
    result += gc_reclaim_page(pool, page);
    page = page->next;
  }
  
  return result - nfree;
}

static void ** stack_base;

static void gc_start_trace(void * ptr) {
  stack_base = &ptr;
  gc_trace(ptr);
}

void gc_trace(void * ptr) {
  if (!ptr) return;
  size_t addr = (size_t)ptr;
  size_t ofs = addr % PAGE_SIZE;
  struct page * page = (struct page *)(addr - ofs);
  struct pool * pool = page->pool;
  size_t size = pool->size;
  size_t idx;
#define CASE(n) else if (size <= n) idx = ofs / ALIGN(n)
  if(false);
  CASE(4); CASE(8); CASE(16); CASE(32); CASE(48); CASE(64); CASE(80); CASE(96);
  CASE(112); CASE(128); CASE(144); CASE(160); CASE(176); CASE(192); CASE(208);
  CASE(224); CASE(240); CASE(256);
  else idx = ofs / ALIGN(size);

  if (!bit_test(page->bitmap, idx)) {
    if (&ptr - stack_base >= 0x10000 || stack_base - &ptr >= 0x10000) {
      /* stack has got too deep - add the pointer as a new root */
      gc_root(ptr);
    } else {
      bit_set(page->bitmap, idx);
      pool->tracer(ptr);
    }
  }
}

static int ncollects = 0;

size_t gc() {
  gc_stats();
  struct pool * pool = pools;
  while (pool) {
    struct page * page = pool->pages;
    while(page) {
      memset(page->bitmap, 0, BITMAP_SIZE);
      page = page->next;
    }
    pool = pool->next;
  }

  size_t i;
  size_t old_gc_nroots = gc_nroots;
  for (i = 0; i < gc_nroots; i++)
    /* OBS gc_start_trace can add new roots */
    if (gc_roots[i])
      gc_start_trace(gc_roots[i]);
    else
      i++; /* skip stack frame */
  gc_nroots = old_gc_nroots;

  size_t result = 0;
  pool = pools;
  while (pool) {
    result += gc_trim(pool) * pool->size;
    pool = pool->next;
  }
  ncollects++;
  gc_stats();
  return result;
}

int gc_frames() {
  int result = 0;
  int i;
  for (i = 0; i < gc_nroots; i++)
    if (!gc_roots[i])
      result++;
  return result;
}

void gc_stats() {
  return;
  printf("%d collections so far\n", ncollects);
  printf("%d roots in %d stack frames\n", gc_nroots - gc_frames(), gc_frames());

  struct pool * pool = pools;
  while(pool) {
    printf("Pool (size %ld): %ld pages, %ld free objects\n",
           pool->size, pool->npages, pool->nfree);
    pool = pool->next;
  }
}
