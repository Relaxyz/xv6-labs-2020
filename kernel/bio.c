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

extern uint ticks;

#define NBUCKET 13

struct bucket{
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket buckets[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  char lockname[16];
  for(int i = 0; i < NBUCKET; i++){
    snprintf(lockname, sizeof(lockname), "bucket%d", i);
    initlock(&bcache.buckets[i].lock, lockname);
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
  }

  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    b->refcnt = 0;
    b->dev = -1;
    b->blockno = 0;
    b->timestamp = 0;
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int h = blockno % NBUCKET;

  // 1. 尝试在目标桶中命中查找
  acquire(&bcache.buckets[h].lock);
  for(b = bcache.buckets[h].head.next; b != &bcache.buckets[h].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[h].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[h].lock);

  // 2. 没找到，准备驱逐。获取全局锁以串行化驱逐过程
  acquire(&bcache.lock);

  // 3. 二次检查：防止在获取全局锁前块已被其他进程加载
  acquire(&bcache.buckets[h].lock);
  for(b = bcache.buckets[h].head.next; b != &bcache.buckets[h].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[h].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[h].lock);

// 4. 寻找候选块
  struct buf *candidate = 0;
  uint mintimestamp = 0xffffffff;
  int candidate_h = -1;

  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.buckets[i].lock);
    int found_better_in_this_bucket = 0;
    for(b = bcache.buckets[i].head.next; b != &bcache.buckets[i].head; b = b->next){
      if(b->refcnt == 0 && b->timestamp <= mintimestamp){
        mintimestamp = b->timestamp;
        found_better_in_this_bucket = 1;
        // 如果之前在别的桶找到了候选，先释放那个旧桶的锁
        if(candidate && candidate_h != i) {
          release(&bcache.buckets[candidate_h].lock);
        }
        candidate = b;
        candidate_h = i;
      }
    }
    // 关键：如果在这个桶里找到了目前最好的 candidate，我们【保持持有】这个桶锁
    if(!found_better_in_this_bucket || candidate_h != i){
      release(&bcache.buckets[i].lock);
    }
  }

  if(!candidate) 
    panic("bget: no buffers");

  // 5. 此时我们必然持有 bcache.lock 和 buckets[candidate_h].lock
  // 且由于一直没放手，candidate->refcnt 绝对不可能变为非 0

  if(candidate_h != h) {
    acquire(&bcache.buckets[h].lock);
    
    // 摘除
    candidate->next->prev = candidate->prev;
    candidate->prev->next = candidate->next;
    release(&bcache.buckets[candidate_h].lock);

    // 插入新桶 h
    candidate->next = bcache.buckets[h].head.next;
    candidate->prev = &bcache.buckets[h].head;
    bcache.buckets[h].head.next->prev = candidate;
    bcache.buckets[h].head.next = candidate;
  } else {
    // 如果 candidate 就在 h 桶，我们已经持有了它的锁（即 buckets[candidate_h].lock）
  }

  // 6. 更新元数据
  candidate->dev = dev;
  candidate->blockno = blockno;
  candidate->valid = 0;
  candidate->refcnt = 1;

  // 7. 释放并返回
  release(&bcache.buckets[h].lock);
  release(&bcache.lock);
  acquiresleep(&candidate->lock);
  return candidate;
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
  int h = b->blockno % NBUCKET;
  acquire(&bcache.buckets[h].lock);
  b->refcnt--;
  if(b->refcnt == 0){
    b->timestamp = ticks;
  }
  release(&bcache.buckets[h].lock);
}

void
bpin(struct buf *b) {
  int h = b->blockno % NBUCKET;
  acquire(&bcache.buckets[h].lock);
  b->refcnt++;
  release(&bcache.buckets[h].lock);
}

void
bunpin(struct buf *b) {
  int h = b->blockno % NBUCKET;
  acquire(&bcache.buckets[h].lock);
  b->refcnt--;
  release(&bcache.buckets[h].lock);
}


