// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 17
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct spinlock bucketlock[NBUCKET];
  struct buf* bucket[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;
  struct spinlock* sp;
  initlock(&bcache.lock, "bcache");
  for(sp=bcache.bucketlock; sp<bcache.bucketlock+NBUCKET; sp++) {
    initlock(sp, "bcache.bucket");
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = b;
    b->prev = b;
    initsleeplock(&b->lock, "buffer");
  }
}

uint 
hash(uint a, uint b) {
  return ((a * 31) + (b * 19))%NBUCKET;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *ed, *noused = 0;
  uint key = hash(dev, blockno), key1;
  acquire(&bcache.bucketlock[key]);
  ed = b = bcache.bucket[key];
  if(ed) do {
    if (b->blockno == blockno && b->dev == dev) {
      b->refcnt++;
      release(&bcache.bucketlock[key]);
      acquiresleep(&b->lock);
      return b;
    }
    if (b->refcnt == 0) noused = b;
    b = b->next;
  } while (b != ed);

  // 没有找到，在本桶中存在未使用的缓存块，直接替换。
  if (noused) {
    noused->dev = dev;
    noused->blockno = blockno;
    noused->valid = 0;
    noused->refcnt = 1;
    release(&bcache.bucketlock[key]);
    acquiresleep(&noused->lock);
    return noused;
  }
  release(&bcache.bucketlock[key]);
  
  // 否则需要其他块加入本桶中
  acquire(&bcache.lock);
  // 先找从来没有缓存的块
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    if (b->dev == 0 && b->blockno == 0) { // 从来没有使用过，没在任何桶内
      acquire(&bcache.bucketlock[key]);
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      ed = bcache.bucket[key]; // 前面释放过锁，重新获取一下数据。
      if (ed == 0) {
        bcache.bucket[key] = b;
      } else {
        b->prev = ed;
        b->next = ed->next;
        b->prev->next = b;
        b->next->prev = b;
      }
      release(&bcache.bucketlock[key]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 再找其他桶内的块
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  for (int i=0; i<NBUF; i++) {
    b = bcache.buf+(i+key)%NBUF;
    // 锁住这个块的桶，在判断是否引用过
    key1 = hash(b->dev, b->blockno);
    acquire(&bcache.bucketlock[key1]);
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      if (key1 != key) { // 不是当前桶，如果是当前桶就可以直接返回
        // 断开原有桶的连接
        struct buf *prev = b->prev, *next = b->next;
        prev->next = next;
        next->prev = prev;
        b->next = b->prev = b;
        bcache.bucket[key1] = next; // 原桶指向下一个元素
        if (next == b) bcache.bucket[key1] = 0; // 原本只有一个，就为空
        
        // 将b添加到当前桶
        acquire(&bcache.bucketlock[key]);
        ed = bcache.bucket[key]; // 前面释放过锁，重新获取一下桶中环形链表。
        if (ed == 0) {
          bcache.bucket[key] = b;
        } else {
          b->prev = ed;
          b->next = ed->next;
          b->prev->next = b;
          b->next->prev = b;
        }
        release(&bcache.bucketlock[key]);
      }
      release(&bcache.bucketlock[key1]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
    release(&bcache.bucketlock[key1]);
  }
  release(&bcache.lock);

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  uint key = hash(b->dev, b->blockno);
  acquire(&bcache.bucketlock[key]);
  b->refcnt--;
  if (b->refcnt<0) panic("brelse");
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // 不需要移出哈希表
    // struct buf *prev = b->prev, *next = b->next;
    // prev->next = next;
    // next->prev = prev;
    // b->next = b->prev = b;
  }
  
  release(&bcache.bucketlock[key]);
}

void
bpin(struct buf *b) {
  uint key = hash(b->dev, b->blockno);
  acquire(&bcache.bucketlock[key]);
  b->refcnt++;
  release(&bcache.bucketlock[key]);
}

void
bunpin(struct buf *b) {
  uint key = hash(b->dev, b->blockno);
  acquire(&bcache.bucketlock[key]);
  b->refcnt--;
  release(&bcache.bucketlock[key]);
}

void
bcachedump() {
  printf("\n==== bcache ====\n");
  for (int i=0; i<NBUCKET; i++) {
    acquire(&bcache.bucketlock[i]);
    struct buf *b, *ed;;
    ed = b = bcache.bucket[i];
    printf("bucket %d:\n", i);
    if(ed) do {
      printf("  dev: %d, blockno: %d, refcnt: %d, valid: %d\n", b->dev, b->blockno, b->refcnt, b->valid);
      b = b->next;
    } while (b != ed);
    release(&bcache.bucketlock[i]);
  }
}

