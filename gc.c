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

struct node {
  struct node * next;
};

static int nroots = 0;
static int maxroots = 0;
void ** roots = 0;

void gc_push_root(void * root) {
  if (nroots == maxroots) {
    if (maxroots) maxroots *= 2;
    else maxroots = 1;
    roots = realloc(roots, maxroots);
    if (!roots) fatal("out of memory for roots");
  }
  roots[nroots++] = root;
}

void gc_pop_root(void * root) {
  if (nroots == 0) fatal("gc_pop_root mismatch (nroots == 0)");
  nroots--;
  if (roots[nroots] != root) fatal("gc_pop_root mismatch");
}

#define PAGE_SIZE 4096
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
  return result;
}

static void gc_new_page(pool_t * pool) {
  assert(sizeof(struct page) == PAGE_SIZE);
  struct page * page = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (page == MAP_FAILED) fatal("mmap failed");
  /* page->bitmap is already zeroed */
  page->pool = pool;
  page->next = pool->pages;
  pool->pages = page;
  gc_reclaim_page(pool, page);
}

static size_t gc_pages(pool_t * pool) {
  size_t i = 0;
  struct page * page = pool->pages;
  while (page) {
    i++;
    page = page->next;
  }
  return i;
}

static void gc_reserve(pool_t * pool) {
  if (pool->free) return;
  size_t freed = gc();
  if (!pool->free || gc_pages(pool) >= 2 * freed / PAGE_SIZE) {
    size_t pages = gc_pages(pool);
    size_t i;
    for (i = 0; i < pages+1; i++)
      gc_new_page(pool);
  }
}

void * gc_alloc(pool_t * pool) {
  gc_reserve(pool);
  struct node * free = pool->free;
  if (!free) return NULL;
  pool->free = free->next;
  return free;
}

static pool_t * pools;

static size_t gc_used(pool_t * pool) {
  size_t result = 0;
  struct node * free = pool->free;
  while (free) {
    result++;
    free = free->next;
  }
  return result;
}

static size_t gc_trim(pool_t * pool) {
  size_t old = gc_used(pool);
  pool->free = NULL;
  pool->next = NULL;

  size_t result = 0;

  struct page * page = pool->pages;
  while (page) {
    result += gc_reclaim_page(pool, page);
    page = page->next;
  }
  
  return (result - old) * pool->size;
}

void gc_trace(void * ptr) {
  if (!ptr) return;
  size_t addr = (size_t)ptr;
  size_t ofs = addr % PAGE_SIZE;
  struct page * page = (struct page *)(addr - ofs);
  struct pool * pool = page -> pool;
  size_t idx = ofs / pool->size;

  if (!pool->next && pool != pools) {
    pool->next = pools;
    pools = pool;
  }
  
  if (!bit_test(page->bitmap, idx)) {
    bit_set(page->bitmap, idx);
    pool->tracer(ptr);
  }
}

size_t gc() {
  size_t i, result = 0;
  pools = NULL;
  for (i = 0; i < nroots; i++)
    gc_trace(roots[i]);
  while (pools) {
    result += gc_trim(pools);
    pools = pools->next;
  }
  return result;
}
