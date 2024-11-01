// Physical memory allocator, for user processes, 
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
// defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

static int kmem_init_phase = 0;  // 标识是否处于初始化阶段

void
kinit()
{
  // 初始化每个 CPU 的锁和空闲链表
  for(int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
    kmem[i].freelist = 0;
  }
  kmem_init_phase = 1;  // 进入初始化阶段
  freerange(end, (void*)PHYSTOP);
  kmem_init_phase = 0;  // 初始化阶段结束
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;

  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    kfree(p);  // 释放每一页
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc(). (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int cpu;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk 
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  if(kmem_init_phase){
    // 在初始化期间，轮流将页面分配给各个CPU
    static int init_cpu = 0;
    cpu = init_cpu;
    init_cpu = (init_cpu + 1) % NCPU;
  } else {
    // 初始化后，释放到当前CPU的空闲链表
    cpu = cpuid();
  }

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu = cpuid();

  // 尝试从当前CPU的空闲链表中分配
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);

  if(!r) {
    // 如果当前CPU的空闲链表为空，尝试从其他CPU窃取
    for(int i = 0; i < NCPU; i++) {
      if(i == cpu)
        continue;
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if(r) {
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // Fill with junk
  return (void*)r;
}
