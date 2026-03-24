#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;
// 同步屏障：当一个线程到达屏障时，它会等待直到所有线程都到达屏障，然后才继续执行。
struct barrier {
  pthread_mutex_t barrier_mutex;  // 互斥锁：保护共享状态
  pthread_cond_t barrier_cond;    // 条件变量：实现等待/通知
  int nthread;                    // 当前已到达屏障的线程数
  int round;                      // 当前轮次（防止虚假唤醒）
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}
// 同步屏障：当一个线程到达屏障时，它会等待直到所有线程都到达屏障，然后才继续执行。
//         这样可以确保所有线程在同一时间点上继续执行，避免了某些线程过早地执行后续代码而导致的错误。
static void 
barrier()
{
  pthread_mutex_lock(&bstate.barrier_mutex);
  if (++bstate.nthread < nthread)
  {
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex); // 等待，直到所有线程都到达屏障,这个函数会自动释放互斥锁，并在被唤醒时重新获取互斥锁
  }
  else
  {
    bstate.nthread = 0; // 重置线程数，为下一轮屏障做准备
    bstate.round++;     // 增加轮次，防止虚假唤醒
    pthread_cond_broadcast(&bstate.barrier_cond); // 唤醒所有等待的线程
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}
// 这个函数的实现思路是：每个线程调用barrier()时，首先获取互斥锁，增加已到达屏障的线程数。
// 如果当前线程是最后一个到达屏障的线程，则重置线程数和轮次，并通知所有等待的线程继续执行。
// 否则，当前线程需要等待，直到轮次发生变化（表示所有线程都已到达屏障并继续执行）。
static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0); // 创建线程，并设置为runnable，传入线程编号作为参数
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0); // 等待线程结束，回收资源
  }
  printf("OK; passed\n");
}
