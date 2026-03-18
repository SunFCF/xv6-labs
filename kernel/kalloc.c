// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// 用于访问物理页引用计数数组
#define PA2PGREF_ID(p) (((p)-KERNBASE)/PGSIZE) // 将物理地址转换为页引用计数数组的索引，KERNBASE是内核虚拟地址的起始地址，PGSIZE是每页的大小
#define PGREF_MAX_ENTRIES PA2PGREF_ID(PHYSTOP) // 页引用计数数组的最大条目数，PHYSTOP是物理内存的结束地址
struct spinlock pgreflock;              // 用于 pageref 数组的锁，防止竞态条件引起内存泄漏
int pageref[PGREF_MAX_ENTRIES];         // 从 KERNBASE 开始到 PHYSTOP 之间的每个物理页的引用计数
// 通过物理地址获得引用计数
#define PA2PGREF(p) pageref[PA2PGREF_ID((uint64)(p))] // 通过物理地址获取对应的引用计数
// 在上面定义了锁，防止多个进程同时修改同一个页的引用计数，导致内存泄漏或过早释放物理页
// 不加锁的例子：
// 比如，父进程的一个物理页p，此时引用数是1。
// 父进程执行fork创建子进程，此时p的引用数是2。
// 父进程执行写操作，触发页面异常。因为p的引用数大于1，开始执行复制p的操作（p的引用数还是2）。
// 进程调度，切换到子进程（父进程复制操作中断）。子进程马上执行exec，释放所有旧的物理页，物理页引用数-1（p的引用数为1）。
// 进程调度，切换到父进程。父进程继续复制p的数据到新的物理页。复制完毕，p的引用数-1。之后回到kama_uvmcowcopy函数中，
// 通过uvmunmap解除了对p的映射。物理页p并没有被释放回收，所有进程都丢失了对p的映射（页表中均没有指向 p 的页表项），造成内存泄漏
// 因为引用计数为1时不应该再起复制操作了，直接修改页表项将p映射为可写即可。因为竞态条件的存在，打断了父进程的复制操作，导致子进程提前释放了p，父进程继续复制操作时p的引用数已经变为1了，导致p被错误地解除映射了。
void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pgreflock, "pgreflock"); // 初始化页引用计数锁
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 修改为只有当物理页的引用计数为0时才真正释放物理页，否则只是将引用计数减1
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&pgreflock);
  if (--PA2PGREF(pa) <= 0) {
    // 只有当物理页的引用计数为0时才真正释放物理页，否则只是将引用计数减1
    
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  } 
  release(&pgreflock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 修改为分配一个物理页，并将该页的引用计数初始化为1
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
    // 还未映射，不会访问到这个页，不用担心并发访问导致的竞态条件，所以不需要加锁
    PA2PGREF(r) = 1; // 将该页的引用计数
  return (void*)r;
}

// 将物理页的一个引用实复制到一个新物理页上，并将原来页的引用次数减1
// 当引用已经小于等于 1 时，不创建和复制到新的物理页，而是直接返回该页本身
void *
kcopy_n_deref(void *pa)
{
  acquire(&pgreflock);
  
  if (PA2PGREF(pa) <= 1) {
    // 当引用已经小于等于 1 时，不创建和复制到新的物理页，而是直接返回该页本身
    release(&pgreflock);
    return pa;
  }
  // 分配一个新的物理页
  uint64 new_pa = (uint64)kalloc();
  if (new_pa == 0) {
    release(&pgreflock);
    return 0; // 分配失败
  }
  // 将原来页的内容复制到新的物理页上
  memmove((void*)new_pa, pa, PGSIZE);
  // 将原来页的引用次数减1
  PA2PGREF(pa)--;
  release(&pgreflock);
  return (void*)new_pa; // 返回新的物理页地址
}

// 增加一个页的引用次数
void
krefpage(void *pa)
{
  acquire(&pgreflock);
  PA2PGREF(pa)++;
  release(&pgreflock);
}