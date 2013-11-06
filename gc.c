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

static void gc_root_1(void * root) {
  if (nroots == maxroots) {
    if (maxroots) maxroots *= 2;
    else maxroots = 1;
    roots = realloc(roots, maxroots*sizeof(void *));
    if (!roots) fatal("out of memory for roots");
  }
  roots[nroots++] = root;
}

void gc_root(void * root) {
  if (root) gc_root_1(root);
}

void gc_enter(void) {
  gc_root_1(NULL);
}

void gc_leave(void) {
  while (nroots > 0 && roots[nroots-1])
    nroots--;
  if (nroots > 0)
    nroots--;
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
  pool->nfree += result;
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

void * gc_alloc(pool_t * pool) {
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

void gc_trace(void * ptr) {
  if (!ptr) return;
  size_t addr = (size_t)ptr;
  size_t ofs = addr % PAGE_SIZE;
  struct page * page = (struct page *)(addr - ofs);
  struct pool * pool = page -> pool;
  size_t idx = ofs / pool->size;

  if (!bit_test(page->bitmap, idx)) {
    bit_set(page->bitmap, idx);
    pool->tracer(ptr);
  }
}

size_t gc() {
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
  for (i = 0; i < nroots; i++)
    gc_trace(roots[i]);

  size_t result = 0;
  pool = pools;
  while (pool) {
    result += gc_trim(pool) * pool->size;
    pool = pool->next;
  }
  return result;
}
