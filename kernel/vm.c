#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable; // kernel_pagetable 是一个全局变量，表示内核的页表。它是一个指向页表的指针，页表包含 512 个页表项。

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel. 创建一个直接映射的页表，直接映射意味着虚拟地址和物理地址是相同的，这样内核就可以直接访问物理内存。
 * kvm开头的s函数操作的是内核的页表，uvm开头的函数操作的是用户的页表。
 */
void
kvminit()
{
  kernel_pagetable = kvminit_newpgtbl();
  kvmmap(kernel_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
}
// 为每一个进程的内核页表添加映射
void kvm_map_pagetable(pagetable_t pgtbl) {
  // 将各种内核需要的 direct mapping 添加到页表 pgtbl 中。
  
  // uart registers
  kvmmap(pgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(pgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT CLINT 仅在内核启动的时候需要使用到，而用户进程在内核态中的操作并不需要使用到该映射。
  // 而其在PLIC 之前，该映射会在将程序映射到内核虚拟空间时候内存冲突，因此将其注释掉，改为在内核启动时单独映射 CLINT。
  //kvmmap(pgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(pgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(pgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(pgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}
// kvminit_newpgtbl 函数创建一个新的内核页表，并将内核需要的 direct mapping 添加到该页表中。
// 这个函数在每个进程创建时调用，为每个进程分配一个独立的内核页表。
pagetable_t
kvminit_newpgtbl()
{
  pagetable_t pgtbl = (pagetable_t) kalloc();
  memset(pgtbl, 0, PGSIZE);

  kvm_map_pagetable(pgtbl);

  return pgtbl;
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
// 在内核启动时调用 kvminithart 函数来切换到内核的页表，并启用分页。这个函数会将内核页表的物理地址写入 satp 寄存器，并刷新 TLB。
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// 遍历页表，找到对应虚拟地址 va 的页表项（PTE），如果 alloc 不为 0，则在需要时创建新的页表页面。
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)   // 如果虚拟地址 va 超过了最大虚拟地址 MAXVA，说明地址无效，调用 panic 函数终止程序。
    panic("walk");
  //遍历页表的三个级别，从最高级别（level 2）到最低级别（level 0）。在每一级，计算出对应的页表项地址，并检查该页表项是否有效。
  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {  //判断有效位
      pagetable = (pagetable_t)PTE2PA(*pte);  // 如果页表项有效，更新 pagetable 指针指向该页表项对应的物理地址，继续下一层级的遍历。
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0; // 如果页表项无效且 alloc 不为 0，则尝试分配一个新的页表页面。如果分配失败，返回 0。
      memset(pagetable, 0, PGSIZE); // 分配成功后，清空新页表页面的内容。
      *pte = PA2PTE(pagetable) | PTE_V;// 将新页表页面的物理地址写入当前页表项，并设置有效位。
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
// 从页表中查找虚拟地址 va 对应的物理地址，如果没有映射则返回 0。只能用于查找用户页面。
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
// 将虚拟地址 va 映射到物理地址 pa，映射大小为 sz，权限为 perm。这个函数只在引导阶段使用，不会刷新 TLB 或启用分页。
// 修改参数列表，添加 pagetable 参数，表示要修改的页表。
void
kvmmap(pagetable_t pgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(pgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
// 将内核虚拟地址 va 转换为物理地址。这个函数只用于栈上的地址，并且假设 va 是页面对齐的。
uint64
kvmpa(pagetable_t pgtbl, uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  //pte = walk(kernel_pagetable, va, 0);
  pte = walk(pgtbl, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
// 将虚拟地址映射到物理地址，va 是虚拟地址的起始位置，pa 是物理地址的起始位置，size 是映射的大小，perm 是权限标志。
// 函数会调用 walk() 来为每个需要的页表页面分配空间，并设置对应的页表项。如果成功返回 0，否则返回 -1。
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;
  // 计算出映射范围内的起始虚拟地址 a 和结束虚拟地址 last，确保它们都对齐到页面边界。向下对齐页面边界是因为页表项是以页面为单位进行映射的。
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);

  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)// 调用walk函数找到或对应创建页表项
      return -1;
    if(*pte & PTE_V) // 如果该页表项已经有效，说明该虚拟地址已经被映射了，调用 panic 函数终止程序。避免覆盖
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V; // 将物理地址 pa 转换为页表项格式，并与权限标志 perm 和有效位 PTE_V 进行按位或运算，设置到页表项中。
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
// 从虚拟地址 va 开始，移除 npages 页的映射。va 必须是页面对齐的，并且这些映射必须存在。如果 do_free 不为 0，则还会释放对应的物理内存。
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0) // 如果虚拟地址 va 不是页面对齐的，调用 panic 函数终止程序。
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0) // 调用 walk 函数找到对应的页表项，如果找不到，说明映射不存在，调用 panic 函数终止程序。不分配只查找
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0) // 如果页表项无效，说明映射不存在，调用 panic 函数终止程序。
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)// 如果页表项的权限标志只有有效位 PTE_V，说明这是一个非正常叶子页表项，调用 panic 函数终止程序。因为 uvmunmap 只能移除叶子页表项，即直接映射到物理内存的页表项。
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
// 创建一个空的用户页表。如果内存不足，返回 0。
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
// 将用户的 initcode 加载到 pagetable 的地址 0 处，用于第一个进程。sz 必须小于一页。
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// 分配页表项和物理内存，将进程的大小从 oldsz 增长到 newsz，oldsz 和 newsz 不需要是页面对齐的。成功返回新的大小，失败返回 0。
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){// 如果内存分配失败，调用 kfree 释放之前分配的内存，并调用 uvmdealloc 将页表项从 oldsz 到 a 的映射移除，然后返回 0。
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    // 将新分配的物理内存映射到虚拟地址 a 上，权限为可读、可写、可执行和用户访问。如果映射失败，调用 kfree 释放当前分配的内存，并调用 uvmdealloc 将页表项从 oldsz 到 a 的映射移除，然后返回 0。
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// 释放用户页面，将进程的大小从 oldsz 减小到 newsz。
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// 递归地释放页表页面。所有的叶子映射必须已经被移除。
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}
// 递归释放一个内核页表中的所有 mapping，但是不释放其指向的物理页
void
kvm_free_kernelpgtbl(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    uint64 child = PTE2PA(pte);
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){ // 如果该页表项指向更低一级的页表
      // 递归释放低一级页表及其页表项
      kvm_free_kernelpgtbl((pagetable_t)child);
      pagetable[i] = 0;
    }
  }
  kfree((void*)pagetable); // 释放当前级别页表所占用空间
}

// Free user memory pages,
// then free page-table pages.
// 释放用户内存页面，然后释放页表页面。
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
// 给定父进程的页表，将其内存复制到子进程的页表中。复制页表和物理内存。成功返回 0，失败返回 -1。失败时会释放任何已分配的页面。
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
// 将一个页表项标记为用户访问无效。exec 用于用户栈的保护页。
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
// 从内核空间复制数据到用户空间。dstva 是用户空间的虚拟地址，src 是内核空间的数据源地址，len 是要复制的字节数。
// 由低地址向高地址复制数据，确保不会跨页复制数据。
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva); //向下对齐 dstva 到页面边界，得到当前页的起始虚拟地址 va0。
    pa0 = walkaddr(pagetable, va0); // 调用 walkaddr 函数查找对应的物理地址
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0); // 计算出当前页内剩余的字节数 n，确保不会跨页复制数据。
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n); //物理地址 pa0 加上 dstva 在当前页内的偏移，得到实际的物理地址，然后使用 memmove 函数将 src 中的数据复制到该物理地址。

    len -= n;
    src += n;
    dstva = va0 + PGSIZE; // 更新 dstva 指向下一页的起始地址，继续复制剩余的数据。
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
// 从用户空间复制数据到内核空间。srcva 是用户空间的虚拟地址，dst 是内核空间的数据目的地址，len 是要复制的字节数。
// 由低地址向高地址复制数据，确保不会跨页复制数据。

// 原本函数从用户空间复制数据到内核空间，是使用页表和虚拟地址来查找用户空间的物理地址，然后将数据复制到内核空间。、
// 简化后在，进程的内核态页表中维护一个用户态页表映射的副本，这样使得内核态也可以对用户态传进来的指针，（逻辑地址）进行解引用，
// 直接访问用户空间的物理地址，从而实现了从用户空间复制数据到内核空间的功能。这样就不需要在内核态进行页表查找和地址转换，简化了代码逻辑，提高了性能。
/*
* 原版的函数没有没有办法直接使用虚拟地址利用cpu（cpu会使用mmu将虚拟地址转换为物理地址，比软件函数快很多倍）来查找物理地址，因为在内核里是没有用户
* 态页表的，所以只能通过 walkaddr 函数来查找物理地址;改良后将用户程序的虚拟空间，整个映射一个备份到内核空间的一个固定位置，(就是0到PLIC 段的低h地址，因为内核
* 的虚拟地址是从这里上开始的，用户程序映射进来不能破坏内核的映射)，这样内核就可以直接使用虚拟地址来访问用户空间的物理地址上的数据了，
*/
// 为保持用户空间和内核空间同步，每个修改到进程用户页表的位置，都将相应的修改同步到进程内核页表中（fork、exec、growproc、userinit）
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new( pagetable,dst, srcva, len);
  /*
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
  */
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
// 从用户空间复制一个以 null 结尾的字符串到内核空间。srcva 是用户空间的虚拟地址，dst 是内核空间的数据目的地址，max 是要复制的最大字节数。
// 复制过程中会检查是否遇到 null 字符，如果遇到则停止复制并返回成功。如果在复制过程中发生错误（如访问无效地址），则返回 -1。
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
  /*
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
  */
}
// 这两个函数是用来打印页表的内容的，vmprint 函数是入口函数，调用 pgtblprint 函数来递归打印页表的内容。
// 会将当前进程使用的全部页表项打印出来
int
pgtblprint(pagetable_t pagetable, int level)
{
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    
    if (pte & PTE_V) // 如果页表项有效，打印
    {
      printf("..");
      for (int j = 0; j < level; j++)
      {
        printf(" ..");
      }

      printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));

      if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) // 如果页表项不是叶子节点，继续递归打印下一层级的页表内容
      {
        uint64 child = PTE2PA(pte);// 获取子页表的物理地址，才能访问到子页表的内容
        pgtblprint((pagetable_t)child, level + 1);  // 递归调用 pgtblprint 函数，传入子页表的地址和当前级别加 1，以便正确缩进打印输出。
      }
    }
  
  }
  return 0;
}

int
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  return pgtblprint(pagetable, 0); //调用 pgtblprint 函数来递归打印页表的内容，初始级别为 0。
}
// 将 src 页表的一部分页映射关系拷贝到 dst 页表中。
// 只拷贝页表项，不拷贝实际的物理页内存。
int
kvmcopymappings(pagetable_t src, pagetable_t dst, uint64 start, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  // 取整到页面边界，遍历 src 页表中从 start 开始的 sz 大小范围内的页表项，将它们的映射关系复制到 dst 页表中。
  // 对于每个页表项，首先检查它是否存在且有效，如果无效则调用 panic 函数终止程序。然后获取该页表项对应的物理地址和权限标志，并将其映射到 dst 页表中。
  // 如果在映射过程中发生错误，则调用 uvmunmap 函数将已经映射的部分撤销，并返回 -1。
  for(i = PGROUNDUP(start); i < start + sz; i += PGSIZE){
    if((pte = walk(src, i, 0)) == 0)
      panic("kvmcopymappings: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("kvmcopymappings: page not present");
    pa = PTE2PA(*pte);
    // `& ~PTE_U` 表示将该页的权限设置为非用户页
    // 必须设置该权限，RISC-V 中内核是无法直接访问用户页的。
    flags = PTE_FLAGS(*pte) & ~PTE_U;
    if(mappages(dst, i, PGSIZE, pa, flags) != 0){
      goto err;
    }
  }

  return 0;

 err:
  uvmunmap(dst, PGROUNDUP(start), (i - PGROUNDUP(start)) / PGSIZE, 0);
  return -1;
}
// 用于内核页表内程序内存映射与用户页表程序内存映射之间的同步
// 与 uvmdealloc 功能类似，将程序内存从 oldsz 缩减到 newsz。但区别在于不释放实际内存
uint64
kvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);
  }

  return newsz;
}