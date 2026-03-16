#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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

  backtrace(); // 打印调用流程

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
// 设置闹钟，以及时钟中断处理函数的地址
uint64
sys_sigalarm(void)
{
  int n;
  uint64 fn;
  if(argint(0, &n) < 0) // 获取寄存器 a0 中的参数 n 的值，如果获取失败则返回 -1
    return -1;
  if(argaddr(1, &fn) < 0)// 获取寄存器 a1 中的参数 fn 的值，如果获取失败则返回 -1
    return -1;
  
  return sigalarm(n, (void(*)())(fn));
}

// 让内核知道用户程序已经完成了时钟中断处理函数的执行，可以继续被正常调度
uint64
sys_sigreturn(void)
{
	return sigreturn();
}
