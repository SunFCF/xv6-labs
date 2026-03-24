#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define NBUCKET 5
#define NKEYS 100000

struct entry {
  int key;
  int value;
  struct entry *next;
};
struct entry *table[NBUCKET];
int keys[NKEYS];
int nthread = 1;
pthread_mutex_t locks[NBUCKET]; // 每个桶一个锁，保护对桶的访问

double
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void 
insert(int key, int value, struct entry **p, struct entry *n)
{
  struct entry *e = malloc(sizeof(struct entry));
  e->key = key;
  e->value = value;
  e->next = n;
  *p = e;
}

static 
void put(int key, int value)
{
  int i = key % NBUCKET;

  pthread_mutex_lock(&locks[i]); // 加锁，保护对桶的访问
  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&locks[i]); // 解锁
}
// 加入互斥锁，但要降低锁的粒度，不能锁住整个函数，不然实际上就变成了单线程了，失去了多线程的意义。
// 只锁住访问bucket的部分，这样不同线程访问bucket不同时就不会互相干扰了。从整个哈希表一个锁降低到每个 bucket 一个锁。
static struct entry*
get(int key)
{
  int i = key % NBUCKET;

  pthread_mutex_lock(&locks[i]); // 加锁，保护对桶的访问
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) break;
  }
  pthread_mutex_unlock(&locks[i]); // 解锁
  return e;
}

static void *
put_thread(void *xa)
{
  int n = (int) (long) xa; // thread number
  int b = NKEYS/nthread;

  for (int i = 0; i < b; i++) {
    put(keys[b*n + i], n);
  }

  return NULL;
}

static void *
get_thread(void *xa)
{
  int n = (int) (long) xa; // thread number
  int missing = 0;

  for (int i = 0; i < NKEYS; i++) {
    struct entry *e = get(keys[i]);
    if (e == 0) missing++;
  }
  printf("%d: %d keys missing\n", n, missing);
  return NULL;
}
// 当多个线程同时访问同一个桶时，可能会发生数据竞争，导致put和get操作的结果不确定。
// 例如：当两个线程同时向同一个桶中插入数据时，线程一在插入数据时，线程二也在插入数据，这时可能会发生以下情况：
// 1. 线程一先插入数据，线程二后插入数据，这时线程二插入的数据会覆盖线程一插入的数据，导致线程一插入的数据丢失。
// 2. 线程二先插入数据，线程一后插入数据，这时线程一插入的数据会覆盖线程二插入的数据，导致线程二插入的数据丢失。
// 3. 线程一和线程二同时插入数据，这时可能会发生数据竞争，导致插入的数据不确定，可能会出现数据丢失或者数据重复的情况。
// 同样的情况也可能发生在get操作中，当多个线程同时访问同一个桶时，可能会发生数据竞争，导致get操作的结果不确定，可能会出现数据丢失或者数据重复的情况。
// 解决这个问题的方法是使用锁来保护对桶的访问，确保同一时间只有一个线程可以访问同一个桶，从而避免数据竞争的发生。
int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  double t1, t0;
  for (int i = 0; i < NBUCKET; i++) {
    pthread_mutex_init(&locks[i], NULL);
  }

  if (argc < 2) {
    fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);
  assert(NKEYS % nthread == 0);
  for (int i = 0; i < NKEYS; i++) {
    keys[i] = random();
  }

  //
  // first the puts
  //
  t0 = now();
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         NKEYS, t1 - t0, NKEYS / (t1 - t0));

  //
  // now the gets
  //
  t0 = now();
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, get_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d gets, %.3f seconds, %.0f gets/second\n",
         NKEYS*nthread, t1 - t0, (NKEYS*nthread) / (t1 - t0));
}
