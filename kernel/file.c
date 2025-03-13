//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

// inode原有大小不可变 offset+n <= ip->size
// 存在虚拟地址没有挂载物理页，需要跳过该页
int 
munmap_write(struct inode *ip, uint64 addr, int offset, int n) 
{
  // printf("va:%p, off:%d, n:%d\n", (void*)addr, offset, n);
  struct proc *p = myproc();
  pte_t* pte;
  int r;
  int max = 2 * BSIZE; // 每次最多写两块
  int i = 0;
  while(i < n){
    // 如果遇到未映射，跳过一页
    if((pte = walk(p->pagetable, addr+i, 0)) == 0 || (*pte & PTE_V) == 0 || PTE_FLAGS(*pte) == PTE_V) {
      offset += MIN(PGROUNDUP(i+1), n)-i;
      i = MIN(PGROUNDUP(i+1), n);
      continue;
    }

    int n1 = n - i;
    if(n1 > max)
      n1 = max;

    begin_op();
    ilock(ip);
    if ((r = writei(ip, 1, addr + i, offset, n1))>0) {
      // printf("r=%d, addr=%p n1=%d\n", r, (void*)(addr+i), n1);
      offset += r;
    }
    iunlock(ip);
    end_op();

    if(r != n1){
      // error from writei
      break;
    }
    i += r;
  }
  // printf("i:%d, n:%d\n", i, n);
  return (i == n ? n : -1);
}

int 
munmap_range(uint64 lva, uint64 rva) {
  uint64 l, r;
  int sz;
  struct proc *p = myproc();
  struct vma* mp;
  uint64 rt = -1;
  for(mp=p->mmaps; mp < &p->mmaps[NOFILE]; mp++){
    l = mp->addr, r = mp->addr+mp->len;
    if (mp->valid == 0) continue;
    if (l<lva && rva < r) continue; // 中间打孔不行
    if (l<lva) l = lva;
    if (rva<r) r = rva;
    // printf("l:%p r:%p\n", (void*) l, (void*) r);
    // printf("lva:%p rva:%p\n", (void*) lva, (void*) rva);
    // printf("before munmap\n");
    // printf("mp->addr:%p mp->addr+mp->len:%p mp->len:%p\n", (void*) mp->addr, (void*) mp->addr+mp->len, (void*) mp->len);
    // printf("inode offset:%d size:%d\n", mp->offset, mp->fp->ip->size);

    // [l,r) 实际需要unmap范围
    // [mp->addr, mp->addr + mp->len) 已经map的范围
    sz = 0;
    if ((mp->flags&MAP_SHARED) && (mp->prot&PROT_WRITE)) { // 回写
      ilock(mp->fp->ip);
      if (mp->fp->ip->size >= mp->offset + (l-mp->addr)) {
        sz = MIN(r-l, mp->fp->ip->size - (mp->offset + (l-mp->addr)));
      }
      iunlock(mp->fp->ip);
      if (sz && munmap_write(mp->fp->ip, l, mp->offset+(l-mp->addr), sz) != sz) { // [0, mp->offset)是之前unmap的，当前如果需要unmap后半部分则需要再偏移
        return -1;
      }
    }
    
    // 释放，sys_mmap确保了mp->addr是页对齐的，mp->addr+mp->len不一定。
    if (l == mp->addr && mp->addr+mp->len == r) { // 完整删除
      // printf("[munmap] 1 va:%p, npages:%ld\n", (void*) PGROUNDDOWN(l), (PGROUNDUP(r)-PGROUNDDOWN(l))/PGSIZE);
      uvmunmap_skp(p->pagetable, PGROUNDDOWN(l), (PGROUNDUP(r)-PGROUNDDOWN(l))/PGSIZE, 1);
      fileclose(mp->fp);
      mp->valid = 0;
    } else if (mp->addr<l) { // 删除后半部分，第一个页还有没有删除的
      // printf("[munmap] 2 va:%p, npages:%ld\n", (void*) PGROUNDUP(l), (PGROUNDUP(r)-PGROUNDUP(l))/PGSIZE);
      uvmunmap_skp(p->pagetable, PGROUNDUP(l), (PGROUNDUP(r)-PGROUNDUP(l))/PGSIZE, 1);
    } else { // 删除前半部分，最后一页还有没有删除的
      // printf("[munmap] 3 va:%p, npages:%ld\n", (void*) PGROUNDDOWN(l), (PGROUNDDOWN(r)-PGROUNDDOWN(l))/PGSIZE);
      uvmunmap_skp(p->pagetable, PGROUNDDOWN(l), (PGROUNDDOWN(r)-PGROUNDDOWN(l))/PGSIZE, 1);
    }
    
    if(lva <= mp->addr) { // 写前半部分，需要增加偏移
      mp->offset += sz; 
      mp->addr = r;
    }
    mp->len -= r-l;
    // printf("after munmap\n");
    // printf("mp->addr:%p mp->addr+mp->len:%p mp->len:%p\n", (void*) mp->addr, (void*) mp->addr+mp->len, (void*) mp->len);
    // printf("inode offset:%d size:%d\n", mp->offset, mp->fp->ip->size);
    rt = 0;
  }
  return rt;
}