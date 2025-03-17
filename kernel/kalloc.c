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
  int free_cnt;
} kmem[NCPU];

void
kinit()
{
  for (int i=0; i<NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
  kmemdump();
}

void
freerange(void *pa_start, void *pa_end) // 此函数只有在main中调用过，所以无需关闭中断。
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  kmem[id].free_cnt++;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock); // 中断关闭，确保当前进程的cpu不变
  r = kmem[id].freelist;
  if(r) {
    kmem[id].freelist = r->next;
    kmem[id].free_cnt--;
  }
  
  release(&kmem[id].lock);
  
  if(!r) {
    // 尝试获取其他cpu的锁，当前cpu已经无页，所以其他进程获取了也会很快释放
    for (int i=0; i<NCPU; i++) {
      acquire(&kmem[i].lock);
      if (kmem[i].freelist) { // 有个cpu有页，这个不是自身
        acquire(&kmem[id].lock); // 已经关闭中断
        r = kmem[i].freelist; // 先拿走一页，防止当cpu获取到后又被其他cpu偷走
        
        kmem[id].freelist = kmem[i].freelist->next;
        kmem[i].freelist = 0;

        kmem[id].free_cnt = kmem[i].free_cnt-1;
        kmem[i].free_cnt = 0;

        release(&kmem[id].lock);
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }
  
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void kmemdump() {
  printf("\n==== kmem ====\n");
  for (int i=0; i<NCPU; i++) {
    acquire(&kmem[i].lock);
    printf("cpuid:%d freepg:%d\n", i, kmem[i].free_cnt);
    release(&kmem[i].lock);
  }
}