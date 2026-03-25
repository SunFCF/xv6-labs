struct buf {
  int valid;   // has data been read from disk? 标记该缓冲区是否包含有效数据
  int disk;    // does disk "own" buf?  标记该缓冲区是否已被修改但尚未写回磁盘
  uint dev;    // Device number. 设备号
  uint blockno;// Block number. 块号
  struct sleeplock lock;// buf->lock（睡眠锁）保护单个缓冲区的数据内容（data、valid、disk标志）
  uint refcnt; // Reference count. 该缓冲区被多少个进程使用，0 表示未被使用，可以被回收
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
  uint lastuse; // 追踪(LRU)：最近最久未使用且引用次数为0的缓冲区（最后一次被使用的时间戳，单位是ticks，ticks数是递增的）
};

