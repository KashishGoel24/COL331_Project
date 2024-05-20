// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

#define ARRAY_SIZE ( PHYSTOP >> PTXSHIFT ) 

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct rmap{
  uint ref_count;         //number of processes that are currently referencing the memory page
  int procRefList[NPROC];    //list of processes that are associated with the physical page
} ;

struct {
  struct spinlock lock;
  int use_lock;
  uint num_free_pages;  //store number of free pages
  struct run *freelist;

  struct rmap rmap_list[ARRAY_SIZE];  // change this
} kmem;

void reInitialisermap(int index){
  kmem.rmap_list[index].ref_count = 0;
  for(int j = 0; j < NPROC; j++){
      kmem.rmap_list[index].procRefList[j] = -1;
  }
}

void reInitialisermap2(int index){
  kmem.rmap_list[index].ref_count = 0;
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  // change here 
  // havent added the kmem.num_free_pages = 0 waali line here cause idts it should be reqd
  for(int i = 0; i < ARRAY_SIZE; i++)
  {
    kmem.rmap_list[i].ref_count = 0;
    for(int j = 0; j < NPROC; j++){
      kmem.rmap_list[i].procRefList[j] = -1;
    }
  }
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
  {
    kfree(p,-1);
    // kmem.num_free_pages+=1;
  }
    
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// void
// kfree(char *v)
// {
//   struct run *r;

//   if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
//     panic("kfree");

//   // Fill with junk to catch dangling refs.
//   memset(v, 1, PGSIZE);

//   if(kmem.use_lock)
//     acquire(&kmem.lock);
//   r = (struct run*)v;
//   r->next = kmem.freelist;
//   kmem.num_free_pages+=1;
//   kmem.freelist = r;
//   if(kmem.use_lock)
//     release(&kmem.lock);
// }
void
kfree(char *v, int process_pid)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // memset(v, 1, PGSIZE);
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  if (kmem.rmap_list[V2P(v) >> PTXSHIFT].ref_count < 0){panic("kfree mein hoon with negative ref count");}
  if(kmem.rmap_list[V2P(v) >> PTXSHIFT].ref_count > 0) {
    kmem.rmap_list[V2P(v) >> PTXSHIFT].ref_count -= 1;
    if (process_pid != -1){
      for (int i = 0 ; i < NPROC ; i++){
        if (kmem.rmap_list[V2P(v) >> PTXSHIFT].procRefList[i] == process_pid){
          kmem.rmap_list[V2P(v) >> PTXSHIFT].procRefList[i] = -1;
        }
      }
    }
  }
  if(kmem.rmap_list[V2P(v) >> PTXSHIFT].ref_count == 0){
    reInitialisermap(V2P(v ) >> PTXSHIFT);
    memset(v, 1, PGSIZE);
    r->next = kmem.freelist;
    kmem.num_free_pages+=1;
    kmem.freelist = r;
  }
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.


// char*
// kalloc(void)
// {
//   struct run *r;

//   if(kmem.use_lock)
//     acquire(&kmem.lock);
//   r = kmem.freelist;
//   if(r)
//   {
//     kmem.freelist = r->next;
//     // addition of this line below
//     kmem.rmap_list[V2P((char*)r) >> PTXSHIFT].ref_count = 1;
//     kmem.num_free_pages-=1;
//   }
    
//   if(kmem.use_lock)
//     release(&kmem.lock);
//   return (char*)r;
// }

char* kalloc(int pid){
  // cprintf("in kalloc \n");
  struct run *r;
  // if(kmem.use_lock){
  //   acquire(&kmem.lock);
  // }
  // r = kmem.freelist;
  while (!kmem.freelist){
    // if (kmem.use_lock){
      // release(&kmem.lock);
    // }
    swapOut();
    // if(kmem.use_lock){
      // acquire(&kmem.lock);
    // }
    // r = kmem.freelist;
  }
  // kmem.freelist = r->next;

  // kmem.rmap_list[V2P((char*)r) >> PTXSHIFT].ref_count = 1;
  // if (pid != -1){
  //   kmem.rmap_list[V2P((char*)r) >> PTXSHIFT].procRefList[emptyindexinrmemlist(V2P((char*)r) >> PTXSHIFT)] = pid;
  // }
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    // kmem.rmap_list[V2P((char*)r) >> PTXSHIFT].ref_count = 1;
    kmem.num_free_pages-=1;
  }
    
  if(kmem.use_lock)
    release(&kmem.lock);
  // kmem.num_free_pages-=1;    
  // if(kmem.use_lock){
  //   release(&kmem.lock);
  // }
  return (char*)r;
}

uint 
num_of_FreePages(void)
{
  acquire(&kmem.lock);

  uint num_free_pages = kmem.num_free_pages;
  
  release(&kmem.lock);
  
  return num_free_pages;
}

void increment_ref_count(uint pa) {
  // if(pa < (uint)V2P(end) || p≥ >= PHYSTOP) panic("increment_ref_count_pa");
  // acquire(&kmem.lock);
  kmem.rmap_list[pa >> PTXSHIFT].ref_count += 1;
  //change proc array
  // release(&kmem.lock);
}

void decrement_ref_count(uint pa) {
  // if(pa < (uint)V2P(end) || pa >= PHYSTOP) panic("decrement_ref_count_pa");
  // acquire(&kmem.lock);
  kmem.rmap_list[pa >> PTXSHIFT].ref_count -= 1;
  // release(&kmem.lock);
}

uint get_ref_count(uint pa) {
  // if(pa < (EXTMEM) || pa >= PHYSTOP){
  //   panic("get_ref_count_pa");
  // } 
  // acquire(&kmem.lock);
  uint ref_count = kmem.rmap_list[pa >> PTXSHIFT].ref_count;
  // release(&kmem.lock);
  return ref_count;
}

void updateproclist2(int indexofrmem, int pid){
  kmem.rmap_list[indexofrmem].procRefList[emptyindexinrmemlist(indexofrmem)] = pid;
}

void updateproclist3(int indexofrmem, int pid){
  kmem.rmap_list[indexofrmem].procRefList[mapPIDtoindex(pid)] = -1;
}

void updateproclist4(int indexofrmem){
  kmem.rmap_list[indexofrmem].ref_count = 1;
}

void updateproclist5(int indexofrmem){
  kmem.rmap_list[indexofrmem].ref_count += 1;
}

int* getprocreflist(int index){
  return kmem.rmap_list[index].procRefList;
}

int emptyindexinrmemlist(int index){
  for (int i = 0 ; i < NPROC ; i++){
    if (kmem.rmap_list[index].procRefList[i] == -1){
      // cprintf("returning th emptyy index : %d\n",i);
      return i;
    }
  }
  panic("didnt find an empty index ");
}

