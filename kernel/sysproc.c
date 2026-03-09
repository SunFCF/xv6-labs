#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
//这里是封装系统调用的内核函数实现，系统调用是用户程序与内核交互的接口
//这些函数在 kernel/syscall.c 中被定义，并在 kernel/proc.c 中被调用。

//以fork为例：在用户态有一个fork函数，内核态有一个fork函数，在用户态调用fork，会使用系统调用sys_fork进入内核态去调用内核态的fork函数
//           内核态的fork函数会创建一个新的进程，并返回新进程的PID给用户态的fork函数。
uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;
  if(argint(0, &mask) < 0)      // 从用户态获取系统调用跟踪掩码参数，如果获取失败则
    return -1;
  struct proc *p = myproc();
  p->mask_syscall_trace = mask; // 设置当前进程的系统调用跟踪掩码，用户传入的 mask 参数指定了要跟踪的系统调用编号
  return 0;
}