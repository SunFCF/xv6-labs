// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
// 管理物理内存的分配器，用于用户进程、内核栈、页表页面和管道缓冲区。分配整个 4096 字节的页面。
// 他分配的物理地址 从内核结束 开始，直到物理地址 PHYSTOP（0x88000000）。内存被分成一个个页面，每个页面大小为 4096 字节。分配器维护一个空闲页面的链表，当需要分配页面时，从链表头部取出一个页面；当释放页面时，将其加入链表头部。
// 这里只分配物理内存，不涉及虚拟内存的映射。虚拟内存的映射由页表管理器负责（vm.c的函数），物理内存分配器只负责提供物理页面。
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
} kmem;
// 初始化物理内存分配器，设置锁，并将内核结束地址到物理内存结束地址之间的内存加入空闲链表。
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}
// 将物理地址从 pa_start 到 pa_end 的内存加入空闲链表。首先将 pa_start 向上对齐到页面边界，然后逐页调用 kfree 将每个页面加入空闲链表，直到达到 pa_end。
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
// 释放由 v 指向的物理内存页面，v 通常应该是由 kalloc() 返回的地址（初始化分配器时除外）。
// 首先检查 v 是否是页面对齐的，并且在内核结束地址和物理内存结束地址之间。如果不满足这些条件，调用 panic 终止程序。
// 然后将 v 填充为 1，以帮助检测悬挂引用。最后将 v 转换为一个 run 结构体，并将其加入空闲链表。
void
kfree(void *pa)
{
  struct run *r;
  // 必须页对齐                // 物理地址必须在内核结束之后       // 物理内存结束地址之前
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 分配一个 4096 字节的物理内存页面。返回一个内核可以使用的指针。如果内存无法分配，返回 0。
// 从空闲链表中取出一个页面，如果链表为空则返回 0。分配成功后，将页面填充为 5，以帮助检测悬挂引用。
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
  return (void*)r;
}
