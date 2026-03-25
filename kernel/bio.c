// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 哈希表中的桶号索引。设置质数个桶可以降低哈希冲突的可能性
#define NBUFMAP_BUCKET 13
// 哈希索引
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)

struct {
  //struct spinlock lock;  // bcache.lock保护缓冲区缓存的全局结构（LRU链表、buf分配、refcnt、dev/blockno设置）
  struct spinlock evict_lock; // 驱逐锁，保护驱逐操作（寻找未被使用的buf来回收），避免映射多个buf到同一个磁盘块
  struct buf buf[NBUF];    

  struct buf bufmap[NBUFMAP_BUCKET]; // 哈希表，包含NBUFMAP_BUCKET个桶，每个桶是一个双向链表的头节点（dummy node），链表中的节点是buf数组中的元素，链表中的元素按照dev和blockno的哈希值分布在不同的桶中
  struct spinlock bufmap_locks[NBUFMAP_BUCKET]; // bufmaplock[i]保护bufmap[i]桶的链表操作
} bcache; // 整个缓冲区缓存，包含一个自旋锁和一个 buf 数组，以及一个双向链表的头节点，链表中的节点是 buf 数组中的元素
          // 一个buf就是一个缓冲区，包含一个睡眠锁和一些元数据，以及一个数据数组，数据数组的大小为一个扇区的大小

void
binit(void)
{
  // 初始化锁和链表
  for (int i = 0; i < NBUFMAP_BUCKET; i++)
  {
    initlock(&bcache.bufmap_locks[i], "bufmap");
    bcache.bufmap[i].next = 0;
  }
  // 初始化缓冲区数组
  for (int i = 0; i < NBUF; i++)
  {
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->lastuse = 0;
    b->refcnt = 0;
    // 将所有buf插入到第一个桶的链表中
    b->next = bcache.bufmap[0].next;  
    bcache.bufmap[0].next = b;
  }

  initlock(&bcache.evict_lock, "evict");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// 扫描 buffer 链表，用给定设备号和扇区号来查找缓冲区
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int key = BUFMAP_HASH(dev, blockno); // 计算哈希桶索引
  
  acquire(&bcache.bufmap_locks[key]); // 获取桶锁，保护桶链表的操作
  // 在桶链表中查找是否有对应的缓冲区
  for (b = bcache.bufmap[key].next; b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++; // 增加引用计数，表示有一个进程正在使用这个缓冲区
      release(&bcache.bufmap_locks[key]); // 释放桶锁
      acquiresleep(&b->lock); // 获取缓冲区的睡眠锁，保护缓冲区的数据内容
      return b; // 返回锁定的缓冲区
    }
  }
  // 没有找到对应的缓冲区，需要分配一个新的缓冲区
  release(&bcache.bufmap_locks[key]); // 释放当前桶锁，避免死锁（如果继续持有，在分配新缓冲区时想查找这个桶时，拿不到就会死锁）
  acquire(&bcache.evict_lock); // 获取驱逐锁，保护驱逐操作，避免多个进程同时驱逐同一个缓冲区
  // 释放桶锁-->加驱逐锁的间隙可能创建了blocknod的缓存区块(被其他进程），因此再检查一次,避免重复创建
  for (b = bcache.bufmap[key].next; b; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno) {
      acquire(&bcache.bufmap_locks[key]); // 获取桶锁，保护桶链表的操作
      b->refcnt++; // 增加引用计数，表示有一个进程正在使用这个缓冲区
      release(&bcache.bufmap_locks[key]); // 释放桶锁
      release(&bcache.evict_lock); // 释放驱逐锁
      acquiresleep(&b->lock); // 获取缓冲区的睡眠锁，保护缓冲区的数据内容
      return b; // 返回锁定的缓冲区
    }
  }
  // 仍然没有找到对应的缓冲区，需要驱逐一个未被使用的缓冲区来回收
  struct buf* before_least = 0; // 记录最近最少使用的缓冲区的前一个缓冲区，方便从链表中删除
  uint holding_bucket = -1; // 记录持有桶锁的桶索引
  // 查询所有桶，找到最近最少使用且引用次数为0的缓冲区
  // 这里在查询的时候需要持有桶锁，避免在查询过程中被其他进程修改桶链表，导致找到的缓冲区不准确
  for(int i = 0; i < NBUFMAP_BUCKET; i++)
  {
    acquire(&bcache.bufmap_locks[i]); // 获取桶锁，保护桶链表的操作

    int newfound = 0; // 标记是否找到新的最近最少使用的缓冲区
    for(b = &bcache.bufmap[i]; b->next; b = b->next) // 遍历一个
    {
       if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse))
       {
         before_least = b; // 更新最近最少使用的缓冲区的前一个缓冲区
         newfound = 1; // 标记找到新的最近最少使用的缓冲区
       }

    }
    if(!newfound) { // 没有找到新的最近最少使用的缓冲区，说明这个桶中没有未被使用的缓冲区，继续查询下一个桶
      release(&bcache.bufmap_locks[i]); // 释放桶锁
    }
    else
    {
      if(holding_bucket != -1) release(&bcache.bufmap_locks[holding_bucket]); // 释放之前持有的桶锁
      holding_bucket = i; // 更新持有桶锁的桶索引
    }
  }
  if(!before_least) // 没有找到未被使用的缓冲区，说明缓冲区数量不足
  {
    panic("bget: no buffers");
  }

  b = before_least->next; // 获取最近最少使用的缓冲区
  
  if (holding_bucket != key) // 如果最近最少使用的缓冲区不在目标桶中，需要先将其从原桶中删除，再插入到目标桶中
  {
    // 从原桶中删除
    before_least->next = b->next; 
    release(&bcache.bufmap_locks[holding_bucket]); // 释放原桶锁

    // 插入到目标桶中
    acquire(&bcache.bufmap_locks[key]); // 获取目标桶锁，保护桶链表的操作
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }
  // 更新缓冲区的元数据
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0; // 新分配的缓冲区数据无效，需要从磁盘读取
  b->refcnt = 1; // 引用计数置为1，表示有一个进程正在使用这个缓冲区
  release(&bcache.bufmap_locks[key]); // 释放目标桶锁
  release(&bcache.evict_lock); // 释放驱逐锁
  acquiresleep(&b->lock); // 获取缓冲区的睡眠锁，保护缓冲区的数据内容
  return b; // 返回锁定的缓冲区
}

// Return a locked buf with the contents of the indicated block.
// 找到一个缓冲区，从磁盘读取数据到缓冲区，并返回锁定的缓冲区
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno); // 因为磁盘io很耗时，所以使用的是睡眠锁，bget函数会返回一个锁定的缓冲区
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
// 将缓冲区的数据写回磁盘，必须先锁定缓冲区
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
// 释放一个锁定的缓冲区，并将其移动到最近使用的链表头部，刚释放的缓冲区被认为是最近使用过的，最有可能被再次使用
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno); // 计算哈希桶索引
  acquire(&bcache.bufmap_locks[key]); // 获取桶锁，保护桶链表的操作
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastuse = ticks; // 更新最后一次使用的时间戳，单位是ticks，ticks数是递增的
  }
  
  release(&bcache.bufmap_locks[key]); // 释放桶锁
}
// 将缓冲区固定在内存中，增加引用计数，表示有一个进程正在使用这个缓冲区
void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno); // 计算哈希桶索引
  acquire(&bcache.bufmap_locks[key]); // 获取桶锁，保护桶链表的操作
  b->refcnt++;
  release(&bcache.bufmap_locks[key]); // 释放桶锁
}
// 将缓冲区从内存中解固定，减少引用计数，表示有一个进程不再使用这个缓冲区
void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno); // 计算哈希桶索引
  acquire(&bcache.bufmap_locks[key]); // 获取桶锁，保护桶链表的操作
  b->refcnt--;
  release(&bcache.bufmap_locks[key]); // 释放桶锁
}


