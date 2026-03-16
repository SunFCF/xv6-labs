//
// test program for the alarm lab.
// you can modify this file for testing,
// but please make sure your kernel
// modifications pass the original
// versions of these tests.
//

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

void test0();
void test1();
void test2();
void periodic();
void slow_handler();

int
main(int argc, char *argv[])
{
  test0();
  test1();
  test2();
  exit(0);
}
// 全局变量，用于记录定时器中断处理函数 periodic 被调用的次数
volatile static int count;
// 测试定时器中断是否能够正常触发，并且在处理函数 periodic 中调用 sigreturn 能否正确返回到被中断的程序继续执行
void
periodic()
{
  count = count + 1;
  printf("alarm!\n");
  sigreturn();
}

// tests whether the kernel calls
// the alarm handler even a single time.
// 这个测试程序的目的是验证内核是否能够正确触发时钟中断并调用用户程序注册的时钟处理函数 periodic。
// 1. 首先在 test0 中调用 sigalarm 注册一个时钟处理函数 periodic，并设置时钟周期为 2 ticks
// 2. 然后进入一个循环，等待 periodic 被调用，或者循环执行一定次数后退出
// 3. 在 periodic 中，每次被调用时会将全局变量 count 加 1，并打印 "alarm!"，然后调用 sigreturn 返回到被中断的程序继续执行
// 4. 如果在 test0 的循环中发现 count 大于 0，说明 periodic 已经被调用过一次，测试通过；如果循环结束后 count 仍然为 0，说明 periodic 从未被调用过，测试失败
void
test0()
{
  int i;
  printf("test0 start\n");
  count = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 1000*500000; i++){
    if((i % 1000000) == 0)
      write(2, ".", 1);
    if(count > 0)
      break;
  }
  sigalarm(0, 0);
  if(count > 0){
    printf("test0 passed\n");
  } else {
    printf("\ntest0 failed: the kernel never called the alarm handler\n");
  }
}

void __attribute__ ((noinline)) foo(int i, int *j) {
  if((i % 2500000) == 0) {
    write(2, ".", 1);
  }
  *j += 1;
}

//
// tests that the kernel calls the handler multiple times.
//
// tests that, when the handler returns, it returns to
// the point in the program where the timer interrupt
// occurred, with all registers holding the same values they
// held when the interrupt occurred.
//
// 这个测试程序的目的是验证内核是否能够正确触发时钟中断并调用用户程序注册的时钟处理函数 periodic，并且在 periodic 中调用 sigreturn 能否正确返回到被中断的程序继续执行
// 1. 首先在 test1 中调用 sigalarm 注册一个时钟处理函数 periodic，并设置时钟周期为 2 ticks
// 2. 然后进入一个循环，执行一个耗时的计算任务，并在每次调用 foo 时将一个计数器 j 加 1
// 3. 在 periodic 中，每次被调用时会将全局变量 count 加 1，并打印 "alarm!"，然后调用 sigreturn 返回到被中断的程序继续执行
// 4. 如果在 test1 的循环中发现 count 大于等于 10，说明 periodic 已经被调用过至少 10 次，测试通过；如果循环结束后 count 小于 10，说明 periodic 被调用的次数不足，测试失败
// 5. 另外，如果循环结束后 j 的值不等于 i 的值，说明 periodic 中调用 sigreturn 后没有正确返回到被中断的程序继续执行，导致 foo 没有被正确调用，测试失败
void
test1()
{
  int i;
  int j;

  printf("test1 start\n");
  count = 0;
  j = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 500000000; i++){
    if(count >= 10)
      break;
    foo(i, &j);
  }
  if(count < 10){
    printf("\ntest1 failed: too few calls to the handler\n");
  } else if(i != j){
    // the loop should have called foo() i times, and foo() should
    // have incremented j once per call, so j should equal i.
    // once possible source of errors is that the handler may
    // return somewhere other than where the timer interrupt
    // occurred; another is that that registers may not be
    // restored correctly, causing i or j or the address ofj
    // to get an incorrect value.
    printf("\ntest1 failed: foo() executed fewer times than it was called\n");
  } else {
    printf("test1 passed\n");
  }
}

//
// tests that kernel does not allow reentrant alarm calls.
// 这个测试程序的目的是验证内核在处理时钟中断时能够正确防止闹钟处理函数被重入调用
// 1. 首先在 test2 中调用 sigalarm 注册一个时钟处理函数 slow_handler，并设置时钟周期为 2 ticks
// 2. 然后进入一个循环，执行一个耗时的计算任务，并在每次调用 slow_handler 时将一个计数器 count 加 1
// 3. 在 slow_handler 中，每次被调用时会将全局变量 count 加 1，并打印 "alarm!"，然后执行一个耗时的循环来模拟处理过程，
// 最后调用 sigalarm(0, 0) 禁止再次触发时钟事件，并调用 sigreturn 返回到被中断的程序继续执行 
void
test2()
{
  int i;
  int pid;
  int status;

  printf("test2 start\n");
  if ((pid = fork()) < 0) {
    printf("test2: fork failed\n");
  }
  if (pid == 0) {
    count = 0;
    sigalarm(2, slow_handler);
    for(i = 0; i < 1000*500000; i++){
      if((i % 1000000) == 0)
        write(2, ".", 1);
      if(count > 0)
        break;
    }
    if (count == 0) {
      printf("\ntest2 failed: alarm not called\n");
      exit(1);
    }
    exit(0);
  }
  wait(&status);
  if (status == 0) {
    printf("test2 passed\n");
  }
}

void
slow_handler()
{
  count++;
  printf("alarm!\n");
  if (count > 1) {
    printf("test2 failed: alarm handler called more than once\n");
    exit(1);
  }
  for (int i = 0; i < 1000*500000; i++) {
    asm volatile("nop"); // avoid compiler optimizing away loop
  }
  sigalarm(0, 0);
  sigreturn();
}
