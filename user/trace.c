#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
/*  系统调用全流程
  user/user.h:		用户态程序调用跳板函数 trace()  （就是指系统调用对应的用户态的函数）
  user/usys.S:		跳板函数 trace() 使用 CPU 提供的 ecall 指令，调用到内核态
  kernel/syscall.c	到达内核态统一系统调用处理函数 syscall()，所有系统调用都会跳到这里来处理。
  kernel/syscall.c	syscall() 根据跳板传进来的系统调用编号，查询 syscalls[] 表，找到对应的内核函数并调用。
  kernel/sysproc.c	到达 sys_trace() 函数，执行具体内核操作
*/

int
main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];
  // 如果参数数量小于 3，或者第二个参数的第一个字符（系统调用跟踪掩码）不是一个数字字符（说明不是一个数字），则打印使用说明并退出。
  if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }

  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }
  // 将命令行参数中的命令和它的参数准备好，存储在 nargv 数组中，准备调用 exec() 来执行这个命令。
  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }
  exec(nargv[0], nargv);
  exit(0);
}
