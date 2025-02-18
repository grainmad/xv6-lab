// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void superfreerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  struct run *superfreelist;
  int pg_used;
  int pg_total;
  int superpg_used;
  int superpg_total;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSUPER);
  printf("pg freerange: start=%lx, end=%lx, cnt=%ld\n", PGROUNDUP((uint64)end), PHYSUPER, (PHYSUPER-PGROUNDUP((uint64)end))/PGSIZE);
  superfreerange((void*)PHYSUPER, (void*)PHYSTOP);
  printf("superpg freerange: start=%lx, end=%lx, cnt=%ld\n", PHYSUPER, PHYSTOP, (PHYSTOP-PHYSUPER)/SUPERPGSIZE);
  pginfo();
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
    kmem.pg_total++;
  }
  kmem.pg_used = 0;
}
void
superfreerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)SUPERPGROUNDUP((uint64)pa_start);
  for(; p + SUPERPGSIZE <= (char*)pa_end; p += SUPERPGSIZE) {
    superkfree(p);
    kmem.superpg_total++;
  }
  kmem.superpg_used = 0;
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSUPER)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.pg_used--;
  release(&kmem.lock);
}

void
superkfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % SUPERPGSIZE) != 0 || (uint64)pa < PHYSUPER || (uint64)pa >= PHYSTOP)
    panic("superkfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.superfreelist;
  kmem.superfreelist = r;
  kmem.superpg_used--;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    kmem.pg_used++;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void *
superkalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.superfreelist;
  if(r) {
    kmem.superfreelist = r->next;
    kmem.superpg_used++;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, SUPERPGSIZE); // fill with junk
  return (void*)r;
}

void pginfo(void)
{
  acquire(&kmem.lock);
  printf("pginfo: pg:%d/%d, spg:%d/%d\n", kmem.pg_used, kmem.pg_total, kmem.superpg_used, kmem.superpg_total);
  release(&kmem.lock);
}