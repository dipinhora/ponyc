#define PONY_WANT_ATOMIC_DEFS

#include "pool.h"
#include "alloc.h"
#include "../ds/fun.h"
#include "../ds/list.h"
#include "../sched/cpu.h"
#include "ponyassert.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <platform.h>
#include <pony/detail/atomics.h>

#ifdef POOL_USE_MESSAGE_PASSING

/// Allocations this size and above are aligned on this size. This is needed
/// so that the pagemap for the heap is aligned.
#define POOL_ALIGN_INDEX (POOL_ALIGN_BITS - POOL_MIN_BITS)

#ifdef USE_VALGRIND
#include <valgrind/valgrind.h>
#include <valgrind/helgrind.h>
#endif

/// When we mmap, pull at least this many bytes.
#ifdef PLATFORM_IS_ILP32
#  define POOL_MMAP (16 * 1024 * 1024) // 16 MB
#else
#  ifdef PLATFORM_IS_WINDOWS
#    define POOL_MMAP (16 * 1024 * 1024) // 16 MB
#  else
#    define POOL_MMAP (128 * 1024 * 1024) // 128 MB
#  endif
#endif

/// An item on a per-size thread-local free list.
typedef struct pool_item_t
{
  struct pool_item_t* next;
} pool_item_t;

/// A per-size thread-local free list header.
typedef struct pool_local_t
{
  pool_item_t* pool;
  size_t length;
  char* start;
  char* end;
} pool_local_t;

/// An item on an either thread-local list of free blocks.
typedef struct pool_block_t
{
  struct pool_block_t* prev;
  struct pool_block_t* next;
  size_t size;

#if defined(_MSC_VER)
  pool_block_t() { }
#endif
} pool_block_t;

/// A thread local list of free blocks header.
typedef struct pool_block_header_t
{
  pool_block_t* head;
  size_t total_size;
  size_t largest_size;
} pool_block_header_t;

static __pony_thread_local pool_local_t pool_local[POOL_COUNT];
static __pony_thread_local pool_block_header_t pool_block_header;
static __pony_thread_local bool started;

typedef struct virt_alloc_t
{
  void* virt_alloc_base;
  uint32_t scheduler_index;
} virt_alloc_t;

static bool virt_alloc_cmp(virt_alloc_t* a, virt_alloc_t* b)
{
  return a->virt_alloc_base == b->virt_alloc_base;
}

static void virt_alloc_free(virt_alloc_t* obj)
{
  POOL_FREE(virt_alloc_t, obj);
}

// TODO: replace with red-black tree or similar for a more efficient way to check for allocation owner
DECLARE_LIST(virt_alloc_list, virt_alloc_list_t, virt_alloc_t);
DEFINE_LIST(virt_alloc_list, virt_alloc_list_t, virt_alloc_t, virt_alloc_cmp, virt_alloc_free);

#ifdef USE_POOLTRACK
#include "../ds/stack.h"
#include "../sched/cpu.h"

#define POOL_TRACK_FREE ((void*)0)
#define POOL_TRACK_ALLOC ((void*)1)
#define POOL_TRACK_PUSH ((void*)2)
#define POOL_TRACK_PULL ((void*)3)
#define POOL_TRACK_PUSH_LIST ((void*)4)
#define POOL_TRACK_PULL_LIST ((void*)5)
#define POOL_TRACK_MAX_THREADS 64

DECLARE_STACK(pool_track, pool_track_t, void);
DEFINE_STACK(pool_track, pool_track_t, void);

typedef struct
{
  bool init;
  bool internal;
  int thread_id;
  pool_track_t* stack;
} pool_track_info_t;

static __pony_thread_local pool_track_info_t track;
static PONY_ATOMIC(int) track_global_thread_id;
static pool_track_info_t* track_global_info[POOL_TRACK_MAX_THREADS];

static void pool_event_print(int thread, void* op, size_t event, size_t tsc,
  void* addr, size_t size)
{
  if(op == POOL_TRACK_ALLOC)
    printf("%d ALLOC "__zu" ("__zu"): %p, "__zu"\n",
      thread, event, tsc, addr, size);
  else if(op == POOL_TRACK_FREE)
    printf("%d FREE "__zu" ("__zu"): %p, "__zu"\n",
      thread, event, tsc, addr, size);
  else if(op == POOL_TRACK_PUSH)
    printf("%d PUSH "__zu" ("__zu"): %p, "__zu"\n",
      thread, event, tsc, addr, size);
  else if(op == POOL_TRACK_PULL)
    printf("%d PULL "__zu" ("__zu"): %p, "__zu"\n",
      thread, event, tsc, addr, size);
  else if(op == POOL_TRACK_PUSH_LIST)
    printf("%d PUSH LIST "__zu" ("__zu"): "__zu", "__zu"\n",
      thread, event, tsc, (size_t)addr, size);
  else if(op == POOL_TRACK_PULL_LIST)
    printf("%d PULL LIST "__zu" ("__zu"): "__zu", "__zu"\n",
      thread, event, tsc, (size_t)addr, size);
}

static void pool_track(int thread_filter, void* addr_filter, size_t op_filter,
  size_t event_filter)
{
  for(int i = 0; i < POOL_TRACK_MAX_THREADS; i++)
  {
    if((thread_filter != -1) && (thread_filter != i))
      continue;

    pool_track_info_t* track = track_global_info[i];

    if(track == NULL)
      continue;

    Stack* t = (Stack*)track->stack;
    size_t event = 0;

    int state = 0;
    void* op;
    void* addr;
    size_t size;
    size_t tsc;

    while(t != NULL)
    {
      for(int j = t->index - 1; j >= 0; j--)
      {
        switch(state)
        {
          case 0:
            tsc = (size_t)t->data[j];
            state = 1;
            break;

          case 1:
            size = (size_t)t->data[j];
            state = 2;
            break;

          case 2:
            addr = t->data[j];
            state = 3;
            break;

          case 3:
          {
            bool print = true;
            op = t->data[j];
            state = 0;

            if((op_filter != (size_t)-1) && (op_filter != (size_t)op))
              print = false;

            if((event_filter != (size_t)-1) && (event_filter != event))
              print = false;

            if((addr_filter != NULL) &&
              ((addr > addr_filter) || ((addr + size) <= addr_filter)))
            {
              print = false;
            }

            if(print)
            {
              pool_event_print(i, op, event, tsc, addr, size);

              if(event_filter != (size_t)-1)
                return;
            }

            event++;
            break;
          }

          default: {}
        }
      }

      t = t->prev;
    }
  }
}

static void track_init()
{
  if(track.init)
    return;

  track.init = true;
  track.thread_id = atomic_fetch_add_explicit(&track_global_thread_id, 1,
    memory_order_seq_cst);
  track_global_info[track.thread_id] = &track;

  // Force the symbol to be linked.
  pool_track(track.thread_id, NULL, -1, 0);
}

static void track_alloc(void* p, size_t size)
{
  track_init();

  if(track.internal)
    return;

  track.internal = true;

  track.stack = pool_track_push(track.stack, POOL_TRACK_ALLOC);
  track.stack = pool_track_push(track.stack, p);
  track.stack = pool_track_push(track.stack, (void*)size);
  track.stack = pool_track_push(track.stack, (void*)ponyint_cpu_tick());

  track.internal = false;
}

static void track_free(void* p, size_t size)
{
  track_init();
  pony_assert(!track.internal);

  track.internal = true;

  track.stack = pool_track_push(track.stack, POOL_TRACK_FREE);
  track.stack = pool_track_push(track.stack, p);
  track.stack = pool_track_push(track.stack, (void*)size);
  track.stack = pool_track_push(track.stack, (void*)ponyint_cpu_tick());

  track.internal = false;
}

#define TRACK_ALLOC(PTR, SIZE) track_alloc(PTR, SIZE)
#define TRACK_FREE(PTR, SIZE) track_free(PTR, SIZE)
#define TRACK_PUSH(PTR, LEN, SIZE) track_push(PTR, LEN, SIZE)
#define TRACK_PULL(PTR, LEN, SIZE) track_pull(PTR, LEN, SIZE)
#define TRACK_EXTERNAL() (!track.internal)

#else

#define TRACK_ALLOC(PTR, SIZE)
#define TRACK_FREE(PTR, SIZE)
#define TRACK_PUSH(PTR, LEN, SIZE)
#define TRACK_PULL(PTR, LEN, SIZE)
#define TRACK_EXTERNAL() (true)

#endif

static void pool_block_remove(pool_block_t* block)
{
  pool_block_t* prev = block->prev;
  pool_block_t* next = block->next;

  if(prev != NULL)
    prev->next = next;
  else
    pool_block_header.head = next;

  if(next != NULL)
    next->prev = prev;
}

static void pool_block_insert(pool_block_t* block)
{
  pool_block_t* next = pool_block_header.head;
  pool_block_t* prev = NULL;

  while(next != NULL)
  {
    if(block->size <= next->size)
      break;

    prev = next;
    next = next->next;
  }

  block->prev = prev;
  block->next = next;

  if(prev != NULL)
    prev->next = block;
  else
    pool_block_header.head = block;

  if(next != NULL)
    next->prev = block;
}

static void* pool_block_get(size_t size)
{
  if(pool_block_header.largest_size >= size)
  {
    pool_block_t* block = pool_block_header.head;

    while(block != NULL)
    {
      if(block->size > size)
      {
        // Use size bytes from the end of the block. This allows us to keep the
        // block info inside the block instead of using another data structure.
        size_t rem = block->size - size;
        block->size = rem;
        pool_block_header.total_size -= size;

        if((block->prev != NULL) && (block->prev->size > block->size))
        {
          // If we are now smaller than the previous block, move us forward in
          // the list.
          if(block->next == NULL)
            pool_block_header.largest_size = block->prev->size;

          pool_block_remove(block);
          pool_block_insert(block);
        } else if(block->next == NULL) {
          pool_block_header.largest_size = rem;
        }

        return (char*)block + rem;
      } else if(block->size == size) {
        if(block->next == NULL)
        {
          pool_block_header.largest_size =
            (block->prev == NULL) ? 0 : block->prev->size;
        }

        // Remove the block from the list.
        pool_block_remove(block);

        // Return the block pointer itself.
        pool_block_header.total_size -= size;
        return block;
      }

      block = block->next;
    }

    // If we didn't find any suitable block, something has gone really wrong.
    pony_assert(false);
  }

  return NULL;
}

static void save_virt_alloc(void* virt_alloc_base, uint32_t scheduler_index, bool remote)
{
  // TODO: save virt_alloc in whatever data structure we're using
  (void)virt_alloc_base;
  (void)scheduler_index;

  // TODO: send virt_alloc info to other scheulder threads if a local allocation
  if(!remote)
    (void)remote;
}

void ponyint_receive_remote_virt_alloc(void* virt_alloc_base, uint32_t scheduler_index)
{
  save_virt_alloc(virt_alloc_base, scheduler_index, true);
}

void ponyint_save_initial_virt_alloc(uint32_t scheduler_index)
{
  save_virt_alloc(pool_block_header.head, scheduler_index, false);
}

static void* pool_alloc_pages(size_t size)
{
  void* p = pool_block_get(size);

  if(p != NULL)
    return p;

  // We have no free blocks big enough.
  if(size >= POOL_MMAP)
  {
    pool_block_t* block = (pool_block_t*)ponyint_virt_alloc(size);
    if(started)
      save_virt_alloc(block, pony_scheduler_index(), false);
    return block;
  }

  pool_block_t* block = (pool_block_t*)ponyint_virt_alloc(POOL_MMAP);
  if(started)
    save_virt_alloc(block, pony_scheduler_index(), false);
  size_t rem = POOL_MMAP - size;

  block->size = rem;
  block->next = NULL;
  block->prev = NULL;
  pool_block_insert(block);
  pool_block_header.total_size += rem;
  if(pool_block_header.largest_size < rem)
    pool_block_header.largest_size = rem;

  return (char*)block + rem;
}

static void pool_free_pages(void* p, size_t size)
{
  if(pool_block_header.total_size >= POOL_MMAP)
  {
    // TODO: ???
  }

  pool_block_t* block = (pool_block_t*)p;
  block->prev = NULL;
  block->next = NULL;
  block->size = size;

  pool_block_insert(block);
  pool_block_header.total_size += size;
  if(pool_block_header.largest_size < size)
    pool_block_header.largest_size = size;
}

static void* pool_get(pool_local_t* pool, size_t index)
{
  // Try per-size thread-local free list first.
  pool_local_t* thread = &pool[index];
  pool_item_t* p = thread->pool;

  if(p != NULL)
  {
    thread->pool = p->next;
    thread->length--;
    return p;
  }

  size_t size = POOL_SIZE(index);

  if(size < POOL_ALIGN)
  {
    // Check our per-size thread-local free block.
    if(thread->start < thread->end)
    {
      void* p = thread->start;
      thread->start += size;
      return p;
    }

    // Use the pool allocator to get a block POOL_ALIGN bytes in size
    // and treat it as a free block.
    char* mem = (char*)pool_get(pool, POOL_ALIGN_INDEX);
    thread->start = mem + size;
    thread->end = mem + POOL_ALIGN;
    return mem;
  }

  // Pull size bytes from the list of free blocks. Don't use a size-specific
  // free block.
  return pool_alloc_pages(size);
}

void* ponyint_pool_alloc(size_t index)
{
#ifdef USE_VALGRIND
  VALGRIND_DISABLE_ERROR_REPORTING;
#endif

  pool_local_t* pool = pool_local;
  void* p = pool_get(pool, index);
  TRACK_ALLOC(p, POOL_MIN << index);

#ifdef USE_VALGRIND
  VALGRIND_ENABLE_ERROR_REPORTING;
  VALGRIND_HG_CLEAN_MEMORY(p, POOL_SIZE(index));
  VALGRIND_MALLOCLIKE_BLOCK(p, POOL_SIZE(index), 0, 0);
#endif

  return p;
}

void ponyint_pool_free(size_t index, void* p)
{
#ifdef USE_VALGRIND
  VALGRIND_HG_CLEAN_MEMORY(p, POOL_SIZE(index));
  VALGRIND_DISABLE_ERROR_REPORTING;
#endif

  pony_assert(index < POOL_COUNT);
  TRACK_FREE(p, POOL_MIN << index);

  // TODO: add logic for identifying and segregating allocations from other threads
  pool_local_t* thread = &pool_local[index];

  pool_item_t* lp = (pool_item_t*)p;
  lp->next = thread->pool;
  thread->pool = lp;
  thread->length++;

#ifdef USE_VALGRIND
  VALGRIND_ENABLE_ERROR_REPORTING;
  VALGRIND_FREELIKE_BLOCK(p, 0);
#endif
}

static void* pool_alloc_size(size_t size)
{
#ifdef USE_VALGRIND
  VALGRIND_DISABLE_ERROR_REPORTING;
#endif

  void* p = pool_alloc_pages(size);
  TRACK_ALLOC(p, size);

#ifdef USE_VALGRIND
  VALGRIND_ENABLE_ERROR_REPORTING;
  VALGRIND_HG_CLEAN_MEMORY(p, size);
  VALGRIND_MALLOCLIKE_BLOCK(p, size, 0, 0);
#endif

  return p;
}

void* ponyint_pool_alloc_size(size_t size)
{
  size_t index = ponyint_pool_index(size);

  if(index < POOL_COUNT)
    return ponyint_pool_alloc(index);

  size = ponyint_pool_adjust_size(size);
  void* p = pool_alloc_size(size);

  return p;
}

static void pool_free_size(size_t size, void* p)
{
#ifdef USE_VALGRIND
  VALGRIND_HG_CLEAN_MEMORY(p, size);
  VALGRIND_DISABLE_ERROR_REPORTING;
#endif

  TRACK_FREE(p, size);
  pool_free_pages(p, size);

#ifdef USE_VALGRIND
  VALGRIND_ENABLE_ERROR_REPORTING;
  VALGRIND_FREELIKE_BLOCK(p, 0);
#endif
}

void ponyint_pool_free_size(size_t size, void* p)
{
  size_t index = ponyint_pool_index(size);

  if(index < POOL_COUNT)
    return ponyint_pool_free(index, p);

  // TODO: add logic for identifying and segregating allocations from other threads
  size = ponyint_pool_adjust_size(size);
  pool_free_size(size, p);
}

void* ponyint_pool_realloc_size(size_t old_size, size_t new_size, void* p)
{
  // Can only reuse the old pointer if the old index/adjusted size is equal to
  // the new one, not greater.

  if(p == NULL)
    return ponyint_pool_alloc_size(new_size);

  size_t old_index = ponyint_pool_index(old_size);
  size_t new_index = ponyint_pool_index(new_size);
  size_t old_adj_size = 0;

  void* new_p;

  if(new_index < POOL_COUNT)
  {
    if(old_index == new_index)
      return p;

    new_p = ponyint_pool_alloc(new_index);
  } else {
    size_t new_adj_size = ponyint_pool_adjust_size(new_size);

    if(old_index >= POOL_COUNT)
    {
      old_adj_size = ponyint_pool_adjust_size(old_size);

      if(old_adj_size == new_adj_size)
        return p;
    }

    new_p = pool_alloc_size(new_adj_size);
  }

  memcpy(new_p, p, old_size < new_size ? old_size : new_size);

  if(old_index < POOL_COUNT)
    ponyint_pool_free(old_index, p);
  else
    pool_free_size(old_adj_size, p);

  return new_p;
}

void ponyint_pool_thread_cleanup()
{
  for(size_t index = 0; index < POOL_COUNT; index++)
  {
    pool_local_t* thread = &pool_local[index];

    while(thread->start < thread->end)
    {
      pool_item_t* item = (pool_item_t*)thread->start;
      thread->start += POOL_SIZE(index);
      item->next = thread->pool;
      thread->pool = item;
      thread->length++;
    }
  }

  // TODO: return allocations to other threads; get allocations from other threads; sort and combine; munmap
  pool_block_header.total_size = 0;
  pool_block_header.largest_size = 0;
}

#endif
