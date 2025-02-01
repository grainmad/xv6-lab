用户跑的程序在用户态，操作系统跑在内核态。  
一般情况下，在用户运行的代码都在用户态，当遇到陷阱或中断时会切换内核执行内核代码，
当遇到中断会被动的进入内核态，比如时钟中断，在处理时钟中断的代码中也许需要进程切换。
而陷阱可以主动触发进入内核态，比如系统调用，它是操作系统提供了一些接口，可以完成一些系统提供的功能，比如读写文件，进程创建等等。

无论时中断还是陷阱，都需要保留当前用户态程序的状态，然后进入内核，返回用户态时需要恢复现场。

syscall trace solution

基于riscv指令集架构实现的xv6 通过ecall指令进入内核态
``` asm
.global trace
trace:
 li a7, SYS_trace
 ecall
 ret
```
进程创建后，首先是在内核态中，经过usertrapret返回到用户态执行，在usertrapret中：
``` c
// send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);
```
设置了ecall指令执行后，进入的代码位置`trampoline_uservec`，这个是每个进程共享的一页物理内存。

在用户态进入内核后，首先执行的是uservec代码
``` asm
.section trampsec
.globl trampoline
.globl usertrap
trampoline:
.align 4
.globl uservec
uservec:    
	#
    # trap.c sets stvec to point here, so
    # traps from user space start here,
    # in supervisor mode, but with a
    # user page table.
    #

    # save user a0 in sscratch so
    # a0 can be used to get at TRAPFRAME.
    csrw sscratch, a0

    # each process has a separate p->trapframe memory area,
    # but it's mapped to the same virtual address
    # (TRAPFRAME) in every process's user page table.
    li a0, TRAPFRAME
    
    # save the user registers in TRAPFRAME
    sd ra, 40(a0)
    sd sp, 48(a0)
    sd gp, 56(a0)
    sd tp, 64(a0)
    sd t0, 72(a0)
    sd t1, 80(a0)
    sd t2, 88(a0)
    sd s0, 96(a0)
    sd s1, 104(a0)
    sd a1, 120(a0)
    sd a2, 128(a0)
    sd a3, 136(a0)
    sd a4, 144(a0)
    sd a5, 152(a0)
    sd a6, 160(a0)
    sd a7, 168(a0)
    sd s2, 176(a0)
    sd s3, 184(a0)
    sd s4, 192(a0)
    sd s5, 200(a0)
    sd s6, 208(a0)
    sd s7, 216(a0)
    sd s8, 224(a0)
    sd s9, 232(a0)
    sd s10, 240(a0)
    sd s11, 248(a0)
    sd t3, 256(a0)
    sd t4, 264(a0)
    sd t5, 272(a0)
    sd t6, 280(a0)

# save the user a0 in p->trapframe->a0
    csrr t0, sscratch
    sd t0, 112(a0)

    # initialize kernel stack pointer, from p->trapframe->kernel_sp
    ld sp, 8(a0)

    # make tp hold the current hartid, from p->trapframe->kernel_hartid
    ld tp, 32(a0)

    # load the address of usertrap(), from p->trapframe->kernel_trap
    ld t0, 16(a0)

    # fetch the kernel page table address, from p->trapframe->kernel_satp.
    ld t1, 0(a0)

    # wait for any previous memory operations to complete, so that
    # they use the user page table.
    sfence.vma zero, zero

    # install the kernel page table.
    csrw satp, t1

    # flush now-stale user entries from the TLB.
    sfence.vma zero, zero

    # jump to usertrap(), which does not return
    jr t0

```
将当前的寄存器都保留在TRAPFRAME中，TRAPFRAME是每个进程都单独分配的一页内存。

随后进入usertrap()中，其中包含了处理系统调用的函数syscall();

``` c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
    if (p->tmask >> num & 1) {
      printf("%d: syscall %s -> %d\n", p->pid, syscalls_name[num], (int) p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```
只需要在当前进程的结构体中添加一个跟踪哪些系统调用的位掩码集合，然后判断当前系统调用号在集合中则输出对应信息。


操作系统具有隔离性，每个程序使用的是虚拟内存，页表可以将虚拟内存映射到物理内存上。用户态代码取址操作实际上会经过硬件将虚拟地址转化为物理地址
不同程序都有单独的页表，所以有单独的虚拟地址空间。
而内核可以看作一个程序，它的页表是将虚拟地址直接映射到物理地址上（大部分虚拟地址是这样的），所以内核代码取址实际上是操作物理地址。

attack solution

根据要求，我们定义LAB_SYSCALL宏。让释放的物理页保存原来的内容，分配新物理页时不初始化。

这样我们可以通过在物理页分配时，打印虚拟内存地址对应的物理内存地址，进而得到藏有秘密的物理页号。

然后我们在新进程中分配物理页，没准会分配到之前进程使用过的页。找到那一页在新进程中的虚拟地址即可。

通过代码可知，剩余内存页的分配和回收，是以一个链表栈组织的，每一物理页的首位置存储了下一个物理页的地址。所以这里的秘密写在一页的首部，那么会被覆写一部分。

分配页将从栈顶获取，但是在回收页的实现中先分配的页先入栈了。