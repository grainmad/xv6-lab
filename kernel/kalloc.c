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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int total_page;
  int used_page;
  uint8* page_ref; // 引用计数数组的指针
  uint64 base_pa; // 物理内存首地址
} kmem;

uint8* get_page_ref(uint64 pa) {
  return &kmem.page_ref[(pa - kmem.base_pa) / PGSIZE];
}

void page_info() {
  acquire(&kmem.lock);
  printf("page used/total = %d/%d\n", kmem.used_page, kmem.total_page);
  uint64 pa = kmem.base_pa;
  for (int i=0; i<kmem.total_page; i++, pa+=PGSIZE) {
    if (kmem.page_ref[i] > 0) {
      printf("page id=%d ptr=%p, ref=%u\n", i, (void*)pa, kmem.page_ref[i]);
    }
  }
  release(&kmem.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);// 0x80000000L ~ end 是 内核代码段的空间
  page_info();
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  
  uint64 l = PGROUNDUP((uint64)pa_start), r = PGROUNDDOWN((uint64)pa_end);
  // 需要x页用于物理页引用计数，设每个页最多引用256次，那么只需一个字节即可，所以一页可以计数PGSIZE个
  // x*PGSIZE >= tot-x --> x >= ceil ( tot/(1+PGSIZE) ) = floor( (tot+PGSIZE)/(1+PGSIZE) ) 
  uint64 x = ((r-l)/PGSIZE+PGSIZE)/(1+PGSIZE);
  kmem.total_page = (r-l)/PGSIZE-x;
  kmem.used_page = (r-l)/PGSIZE-x;
  kmem.page_ref = (uint8*) l;
  kmem.base_pa = l+PGSIZE*x;
  memset(kmem.page_ref, 1, kmem.total_page);
  p = (char*)PGROUNDUP(kmem.base_pa);
  for(; p < (char*)r; p += PGSIZE) {
    kfree(p);
  }
  printf("init mem\n");
  printf("ref cnt range [%p, %p), total_page %ld\n", (void*)l, (void*)kmem.base_pa, x);
  printf("pa range [%p, %p), total_page %d\n", (void*)kmem.base_pa, (void*)PGROUNDDOWN((uint64)pa_end), kmem.total_page);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  uint8* ref = get_page_ref((uint64)pa);
  if (*ref > 0) {
    (*ref)--;
  } else {
    panic("kfree: ref count is 0");
  }
  if (*ref > 0) {
    release(&kmem.lock);
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.used_page--;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// pa exist ref++
void *
kalloc(uint64 pa)
{
  if (pa) {
    acquire(&kmem.lock);
    uint8* ref = get_page_ref(pa);
    (*ref)++;
    release(&kmem.lock);
    return (void*)pa;
  }
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    kmem.used_page++;
    uint8* ref = get_page_ref((uint64)r);
    (*ref)++;
  }
    
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
