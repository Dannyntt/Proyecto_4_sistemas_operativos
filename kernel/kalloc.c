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
  uint64 nfree; // páginas en freelist; también se protege con lock
} kmem;

// Conteo de referencias por página física, desde KERNBASE hasta PHYSTOP.
// Una página con conteo 0 está en la lista libre. Este lock protege solo el
// arreglo; nunca se toma kmem.lock mientras se tiene este (ni al revés), así
// que no hay orden de locks que respetar ni deadlock posible entre ambos.
struct {
  struct spinlock lock;
  int count[(PHYSTOP - KERNBASE) / PGSIZE];
} refs;

#define PAGEIDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refs.lock, "refs");
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    // estas páginas nunca pasaron por kalloc(): se les da la referencia
    // que kfree() va a quitar.
    refs.count[PAGEIDX(p)] = 1;
    kfree(p);
  }
}

// Quita una referencia a la página de memoria física pa. Solo cuando
// no quedan referencias la devuelve a la lista libre.
// (Al inicializar el allocator, freerange() llama a kfree() sobre páginas
// que nunca pasaron por kalloc(); ver kinit más arriba.)
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&refs.lock);
  if (refs.count[PAGEIDX(pa)] < 1)
    panic("kfree: conteo de referencias");
  if (--refs.count[PAGEIDX(pa)] > 0) {
    release(&refs.lock);
    return; // otro proceso todavía la usa
  }
  release(&refs.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.nfree++;
  release(&kmem.lock);
}

// Agrega una referencia a una página que ya está en uso.
void
krefinc(void *pa)
{
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("krefinc");

  acquire(&refs.lock);
  if (refs.count[PAGEIDX(pa)] < 1)
    panic("krefinc: página libre");
  refs.count[PAGEIDX(pa)]++;
  release(&refs.lock);
}

// Número de referencias a la página pa.
int
krefcount(void *pa)
{
  int n;

  acquire(&refs.lock);
  n = refs.count[PAGEIDX(pa)];
  release(&refs.lock);
  return n;
}

// Páginas físicas libres en todo el sistema.
uint64
kfreepages(void)
{
  uint64 n;

  acquire(&kmem.lock);
  n = kmem.nfree;
  release(&kmem.lock);
  return n;
}

// Páginas físicas con más de una referencia.
uint64
ksharedpages(void)
{
  uint64 n = 0;

  acquire(&refs.lock);
  for (int i = 0; i < (PHYSTOP - KERNBASE) / PGSIZE; i++)
    if (refs.count[i] > 1)
      n++;
  release(&refs.lock);
  return n;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) {
    kmem.freelist = r->next;
    kmem.nfree--;
  }
  release(&kmem.lock);

  if (r) {
    // la página salió de la lista libre: nadie más la referencia todavía.
    acquire(&refs.lock);
    refs.count[PAGEIDX(r)] = 1;
    release(&refs.lock);
    memset((char *)r, 5, PGSIZE); // fill with junk
  }
  return (void *)r;
}
