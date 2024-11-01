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

#define NBUCKETS 13  // 哈希桶的数量，选择质数以减少冲突


struct {
  struct spinlock lock[NBUCKETS];  // 每个哈希桶的锁
  struct buf buf[NBUF];

  // 每个哈希桶有自己的双向链表头
  struct buf head[NBUCKETS];
} bcache;

void
binit(void)
{
  struct buf *b;

  // 初始化每个哈希桶的锁和链表头
  for(int i = 0; i < NBUCKETS; i++) {
    char lockname[16];
    snprintf(lockname, sizeof(lockname), "bcache%d", i);
    initlock(&bcache.lock[i], lockname);

    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // 初始化缓冲区，并将它们插入到哈希桶中
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
    b->dev = -1;
    b->blockno = 0;
    b->valid = 0;
    b->refcnt = 0;
    b->next = b->prev = 0;

    // 将缓冲区平均分配到各个哈希桶
    int h = (b - bcache.buf) % NBUCKETS;
    acquire(&bcache.lock[h]);
    b->next = bcache.head[h].next;
    b->prev = &bcache.head[h];
    bcache.head[h].next->prev = b;
    bcache.head[h].next = b;
    release(&bcache.lock[h]);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 计算哈希桶索引
  int h = (dev ^ blockno) % NBUCKETS;

  acquire(&bcache.lock[h]);

  // 块是否已缓存？
  for(b = bcache.head[h].next; b != &bcache.head[h]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock[h]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 未缓存。在当前哈希桶中寻找未被引用的缓冲区
  for(b = bcache.head[h].prev; b != &bcache.head[h]; b = b->prev) {
    if(b->refcnt == 0) {
      // 重用该缓冲区
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[h]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.lock[h]);

  // 在其他哈希桶中寻找未被引用的缓冲区
  for(int i = 0; i < NBUCKETS; i++) {
    if(i == h)
      continue;

    acquire(&bcache.lock[i]);

    for(b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev) {
      if(b->refcnt == 0) {
        // 从旧的哈希桶中移除缓冲区
        b->next->prev = b->prev;
        b->prev->next = b->next;

        // 更新缓冲区信息
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        // 插入到新的哈希桶中
        release(&bcache.lock[i]);
        acquire(&bcache.lock[h]);
        b->next = bcache.head[h].next;
        b->prev = &bcache.head[h];
        bcache.head[h].next->prev = b;
        bcache.head[h].next = b;
        release(&bcache.lock[h]);

        acquiresleep(&b->lock);
        return b;
      }
    }

    release(&bcache.lock[i]);
  }

  // 没有可用的缓冲区
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // 计算哈希桶索引
  int h = (b->dev ^ b->blockno) % NBUCKETS;

  acquire(&bcache.lock[h]);
  b->refcnt--;
  if(b->refcnt == 0) {
    // 没有进程在使用该缓冲区，将其移动到链表头部
    b->next->prev = b->prev;
    b->prev->next = b->next;

    b->next = bcache.head[h].next;
    b->prev = &bcache.head[h];
    bcache.head[h].next->prev = b;
    bcache.head[h].next = b;
  }
  release(&bcache.lock[h]);
}

void
bpin(struct buf *b) {
  int h = (b->dev ^ b->blockno) % NBUCKETS;

  acquire(&bcache.lock[h]);
  b->refcnt++;
  release(&bcache.lock[h]);
}

void
bunpin(struct buf *b) {
  int h = (b->dev ^ b->blockno) % NBUCKETS;

  acquire(&bcache.lock[h]);
  b->refcnt--;
  release(&bcache.lock[h]);
}


