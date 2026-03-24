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
// 空闲链表是外头上插入入的，分配页时从链表头分配，释放页时也从链表头插入，这样可以提高局部性，增加缓存命中率
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // 每个CPU一个物理页分配器，减少锁竞争

char *kmem_lock_names[] = {
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};
// 这里相当于先初始化为一个链表，当其他cpu调度的时候一开始为空链表，就会触发偷取机制，去其他cpu的链表上偷页
void
kinit()
{
  for(int i=0;i<NCPU;i++) { // 初始化所有锁
    initlock(&kmem[i].lock, kmem_lock_names[i]);
  }
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
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off(); // 关闭中断，获取cpuid，避免在释放页时被调度到另一个CPU上，导致错误地将页添加到另一个CPU的freelist中
  int cpu_id = cpuid();

  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;  // 将释放的页添加到本CPU的freelist头部
  kmem[cpu_id].freelist = r;        // 更新本CPU的freelist头部为新添加的页
  release(&kmem[cpu_id].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 将共享的一个freelist修改为多个freelist，每个CPU一个，CPU 并发分配物理页就不再会互相排斥了，提高了并行性
void *
kalloc(void)
{
  struct run *r;

  push_off(); // 关闭中断，获取cpuid，避免在分配页时被调度到另一个CPU上，导致错误地从另一个CPU的freelist中分配页
  int cpu_id = cpuid();

  acquire(&kmem[cpu_id].lock);
  // 如果是当前cpu的freelist空了，就去其他cpu的freelist上偷页
  if (!kmem[cpu_id].freelist){ 
    release(&kmem[cpu_id].lock); // 避免同时持有两个CPU的锁，防止互相偷导致死锁

    int steal_left = 64;       // 偷64个页，减少偷取的频率
    struct run *stolen = 0;    // 临时链表，保存偷来的页
    for(int i = 0; i < NCPU; i++) {
      if(i == cpu_id) // 跳过当前cpu
        continue;
      acquire(&kmem[i].lock); // 只持有一个CPU的锁
      
      struct run *steal_list = kmem[i].freelist; // 记录对方链表头
      while (steal_list && steal_left) // 从其他cpu的freelist上偷页，直到偷够64个或者没有页了
      {
        kmem[i].freelist = steal_list->next; // 把对方链表头取下（对方头指向下一个）
        steal_list->next = stolen; // 先接到临时链表
        stolen = steal_list; // 更新临时链表头
        steal_list = kmem[i].freelist; // 继续取对方新的链表头
        steal_left--;
      }
      release(&kmem[i].lock); // 偷完页后释放其他cpu的锁
      if (steal_left == 0) // 如果已经偷够64个页了，就不再继续偷了
        break;  
    }
    // 重新拿回当前cpu的锁，把偷来的页接到当前cpu的freelist头部
    acquire(&kmem[cpu_id].lock);
    while (stolen) {
      struct run *next = stolen->next;
      stolen->next = kmem[cpu_id].freelist;
      kmem[cpu_id].freelist = stolen;
      stolen = next;
    }
  }
  
  r = kmem[cpu_id].freelist; // 从本CPU的freelist头部分配页
  if(r)    
    kmem[cpu_id].freelist = r->next; // 拿走链表头
  release(&kmem[cpu_id].lock);

  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
