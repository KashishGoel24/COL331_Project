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
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

struct swap_slot {
  int page_perm;
  int is_free;
  int procRefListofPage[NPROC]; 
  int refCountpage;
  int indexInRmap;
};

struct swap_block {
  struct swap_slot slots[SWAPBLOCKS/8];
} swap_block;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.

void writeToDisk(uint dev, char* pg, int blockno){
    struct buf *b;
    for (int i = 0; i < 8 ; i++){
      b = bget(dev, blockno+i);
      memmove(b->data,pg+(i*512),512);   // check if the index pg+i*512 is correct
      bwrite(b);
      brelse(b);
    }
}

void readFromDiskWriteToMem(uint dev, char *pg, uint blockno){
    struct buf* b;
    for (int i = 0 ; i < 8 ; i++){
      b = bread(dev,blockno+i);
      memmove(pg+(i*512),b->data,512);
      // bwrite(b);   //*******INCLUDEED********
      brelse(b);
    }
}


void swapSpaceinit(void){
  // initlock(&swap_block.lock,"swapSpace");
  for (int i = 0 ; i < (SWAPBLOCKS/8) ; i++){
    swap_block.slots[i].is_free = 1;
    swap_block.slots[i].page_perm = 0;
  }
}

int diskBlockNumber(int arrayindex){
  return 8*arrayindex + 2;    // check if disk blocks are 1 indexed or 0 indexed
}

int findVacantSwapSlot(void){
    for (int i = 0 ; i < (SWAPBLOCKS/8) ; i++){
      if (swap_block.slots[i].is_free == 1){
        return i;
      }
    }
    panic("no vacant swap slot found"); // if no slot found -> for debugging
}


void updateSwapSlot(int diskblock, int isFree, int page_perm, int refCount, int* procreflist, int indexinrmap){
  // acquire(&swap_block.lock);
  swap_block.slots[diskblock].page_perm = page_perm;
  swap_block.slots[diskblock].is_free = isFree;
  // *swap_block.slots[diskblock].procRefListofPage = *procreflist;
  for (int i = 0; i < NPROC; i++) {
    if (refCount == 0 && procreflist == 0){
      swap_block.slots[diskblock].procRefListofPage[i] = -1;
    }
    else{
      swap_block.slots[diskblock].procRefListofPage[i] = procreflist[i];
    }
  }
  swap_block.slots[diskblock].refCountpage = refCount;
  swap_block.slots[diskblock].indexInRmap = indexinrmap;
  // if (refCount == 0 && pro)
  // cprintf("value of is_free of the swap slot %d \n",swap_block.slots[diskblock].is_free);
  // release(&swap_block.lock);
}

void clear_swap_slot(pte_t* page, int pid){
  int block_num = *page >> 12;
  int swap_blk = (block_num - 2) / 8;
  if (swap_block.slots[swap_blk].refCountpage == 0){
    if (swap_block.slots[swap_blk].indexInRmap == -1){panic("negative index but 0 ref coitn ");}
    reInitialisermap(swap_block.slots[swap_blk].indexInRmap);
  }
  if (swap_block.slots[swap_blk].refCountpage <= 1){
    swap_block.slots[swap_blk].is_free = 1;
  }
  else{
    swap_block.slots[swap_blk].refCountpage -= 1;
    swap_block.slots[swap_blk].procRefListofPage[mapPIDtoindex(pid)] = -1;
  }
  
}

int getPerm(int diskBlock){
  return swap_block.slots[diskBlock].page_perm;
}

int getrefcount(int diskBlock){
  return swap_block.slots[diskBlock].refCountpage ;
}

int* getProclist(int diskBlock){
  return swap_block.slots[diskBlock].procRefListofPage;
}