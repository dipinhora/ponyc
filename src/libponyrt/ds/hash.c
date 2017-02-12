#include "hash.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DELETED ((void*)1)

// Minimum HASHMAP size allowed
#define MIN_HASHMAP_SIZE 8

static bool valid(void* entry)
{
  return ((uintptr_t)entry) > ((uintptr_t)DELETED);
}

static void* search(hashmap_t* map, size_t* pos, void* key, hash_fn hash,
  cmp_fn cmp)
{
  size_t index_del = map->size;
  size_t mask = index_del - 1;

  size_t h = hash(key);
  size_t index = h & mask;
  void* elem;

  for(size_t i = 1; i <= mask; i++)
  {
    elem = map->buckets[index];

    if(elem == NULL)
    {
      if(index_del <= mask)
        *pos = index_del;
      else
        *pos = index;

      return NULL;
    } else if(elem == DELETED) {
      /* some element was here, remember the first deleted slot */
      if(index_del > mask)
        index_del = index;
    } else if(cmp(key, elem)) {
      *pos = index;
      return elem;
    }

    index = (h + ((i + (i * i)) >> 1)) & mask;
  }

  *pos = index_del;
  return NULL;
}

static void resize(hashmap_t* map, hash_fn hash, cmp_fn cmp, alloc_fn alloc,
  free_size_fn fr)
{
  size_t s = map->size;
  size_t c = map->count;
  void** b = map->buckets;
  bitmap_t* old_item_bitmap = map->item_bitmap;
  void* curr = NULL;

  map->count = 0;
  map->size = (s < MIN_HASHMAP_SIZE) ? MIN_HASHMAP_SIZE : s << 3;

  // use a single memory allocation to exploit spatial memory/cache locality
  size_t bitmap_size = map->size/HASHMAP_BITMAP_TYPE_SIZE +
    (map->size%HASHMAP_BITMAP_TYPE_SIZE==0?0:1);
  void* mem_alloc = alloc((bitmap_size * sizeof(bitmap_t)) +
    (map->size * sizeof(void*)));
  memset(mem_alloc, 0, (bitmap_size * sizeof(bitmap_t)) +
    (map->size * sizeof(void*)));
  map->item_bitmap = (bitmap_t*)mem_alloc;
  map->buckets = (void**)((char *)mem_alloc + (bitmap_size * sizeof(bitmap_t)));

  // use hashmap scan to efficiently copy all items to new bucket array
  size_t i = HASHMAP_BEGIN;
  while((curr = ponyint_hashmap_next(&i, c, old_item_bitmap,
    s, b)) != NULL)
  {
    curr = b[i];

    if(valid(curr))
      ponyint_hashmap_put(map, curr, hash, cmp, alloc, fr);
  }

  if((fr != NULL) && (b != NULL))
  {
    size_t old_bitmap_size = s/HASHMAP_BITMAP_TYPE_SIZE +
      (s%HASHMAP_BITMAP_TYPE_SIZE==0?0:1);
    fr((old_bitmap_size * sizeof(bitmap_t)) + (s * sizeof(void*)),
      old_item_bitmap);
  }

}

static size_t optimize_item(hashmap_t* map, hash_fn hash, alloc_fn alloc,
  free_size_fn fr, cmp_fn cmp, size_t old_index, void* entry)
{

  size_t mask = map->size - 1;

  size_t h = hash(entry);
  size_t index = h & mask;

  for(size_t i = 1; i <= mask; i++)
  {
    // if next bucket index is current position, item is already in optimal spot
    if(index == old_index)
      break;

    // found an earlier deleted bucket so move item
    if(map->buckets[index] == NULL)
    {
      ponyint_hashmap_clearindex(map, old_index);
      ponyint_hashmap_putindex(map, entry, hash, cmp, alloc, fr, index);
      return 1;
    }

    // find next bucket index
    index = (h + ((i + (i * i)) >> 1)) & mask;
  }

  return 0;
}

void ponyint_hashmap_init(hashmap_t* map, size_t size, alloc_fn alloc)
{
  if(size > 0)
  {
    // make sure we have room for this many elements without resizing
    size <<= 1;

    if(size < MIN_HASHMAP_SIZE)
      size = MIN_HASHMAP_SIZE;
    else
      size = ponyint_next_pow2(size);
  }

  map->count = 0;
  map->size = size;

  if(size > 0)
  {
    // use a single memory allocation to exploit spatial memory/cache locality
    size_t bitmap_size = size/HASHMAP_BITMAP_TYPE_SIZE +
      (size%HASHMAP_BITMAP_TYPE_SIZE==0?0:1);
    void* mem_alloc = alloc((bitmap_size * sizeof(bitmap_t)) +
      (size * sizeof(void*)));
    memset(mem_alloc, 0, (bitmap_size * sizeof(bitmap_t)) +
      (size * sizeof(void*)));
    map->item_bitmap = (bitmap_t*)mem_alloc;
    map->buckets = (void**)((char *)mem_alloc +
      (bitmap_size * sizeof(bitmap_t)));
  } else {
    map->buckets = NULL;
    map->item_bitmap = NULL;
  }
}

void ponyint_hashmap_destroy(hashmap_t* map, free_size_fn fr, free_fn free_elem)
{
  if(free_elem != NULL)
  {
    void* curr = NULL;

    // use hashmap scan to efficiently free all items
    size_t i = HASHMAP_BEGIN;
    while((curr = ponyint_hashmap_next(&i, map->count, map->item_bitmap,
      map->size, map->buckets)) != NULL)
    {
      curr = map->buckets[i];

      if(valid(curr))
        free_elem(curr);
    }
  }

  if((fr != NULL) && (map->size > 0))
  {
    size_t bitmap_size = map->size/HASHMAP_BITMAP_TYPE_SIZE +
      (map->size%HASHMAP_BITMAP_TYPE_SIZE==0?0:1);
    fr((bitmap_size * sizeof(bitmap_t)) + (map->size * sizeof(void*)),
      map->item_bitmap);
  }

  map->count = 0;
  map->size = 0;
  map->buckets = NULL;
  map->item_bitmap = NULL;
}

void* ponyint_hashmap_get(hashmap_t* map, void* key, hash_fn hash, cmp_fn cmp,
  size_t* pos)
{
  if(map->count == 0)
    return NULL;

  return search(map, pos, key, hash, cmp);
}

void* ponyint_hashmap_put(hashmap_t* map, void* entry, hash_fn hash, cmp_fn cmp,
  alloc_fn alloc, free_size_fn fr)
{
  if(map->size == 0)
    ponyint_hashmap_init(map, 4, alloc);

  size_t pos;
  void* elem = search(map, &pos, entry, hash, cmp);

  map->buckets[pos] = entry;

  if(elem == NULL)
  {
    map->count++;

    // update item bitmap
    size_t ib_index = pos/HASHMAP_BITMAP_TYPE_SIZE;
    size_t ib_offset = pos%HASHMAP_BITMAP_TYPE_SIZE;
    map->item_bitmap[ib_index] |= ((bitmap_t)1 << ib_offset);

    if((map->count << 1) > map->size)
      resize(map, hash, cmp, alloc, fr);
  }

  return elem;
}

void* ponyint_hashmap_putindex(hashmap_t* map, void* entry, hash_fn hash,
  cmp_fn cmp, alloc_fn alloc, free_size_fn fr, size_t pos)
{
  if(pos == HASHMAP_UNKNOWN)
    return ponyint_hashmap_put(map, entry, hash, cmp, alloc, fr);

  if(map->size == 0)
    ponyint_hashmap_init(map, 4, alloc);

  assert(pos <= map->size);
  void* elem = map->buckets[pos];

  map->buckets[pos] = entry;

  if(elem == DELETED || elem == 0)
  {
    map->count++;

    // update item bitmap
    size_t ib_index = pos/HASHMAP_BITMAP_TYPE_SIZE;
    size_t ib_offset = pos%HASHMAP_BITMAP_TYPE_SIZE;
    map->item_bitmap[ib_index] |= ((bitmap_t)1 << ib_offset);


    if((map->count << 1) > map->size)
      resize(map, hash, cmp, alloc, fr);

    return entry;
  }

  return elem;
}

void* ponyint_hashmap_remove(hashmap_t* map, void* entry, hash_fn hash,
  cmp_fn cmp)
{
  if(map->count == 0)
    return NULL;

  size_t pos;
  void* elem = search(map, &pos, entry, hash, cmp);

  if(elem != NULL)
  {
    map->buckets[pos] = DELETED;
    map->count--;

    // update item bitmap
    size_t ib_index = pos/HASHMAP_BITMAP_TYPE_SIZE;
    size_t ib_offset = pos%HASHMAP_BITMAP_TYPE_SIZE;
    map->item_bitmap[ib_index] &= ~((bitmap_t)1 << ib_offset);
  }

  return elem;
}

void* ponyint_hashmap_removeindex(hashmap_t* map, size_t index)
{
  if(map->size <= index)
    return NULL;

  void* elem = map->buckets[index];

  if(!valid(elem))
    return NULL;

  map->buckets[index] = DELETED;
  map->count--;

  // update item bitmap
  size_t ib_index = index/HASHMAP_BITMAP_TYPE_SIZE;
  size_t ib_offset = index%HASHMAP_BITMAP_TYPE_SIZE;
  map->item_bitmap[ib_index] &= ~((bitmap_t)1 << ib_offset);

  return elem;
}

void* ponyint_hashmap_next(size_t* i, size_t count, bitmap_t* item_bitmap,
  size_t size, void** buckets)
{
  if(count == 0)
    return NULL;

  size_t index = *i + 1;
  size_t ib_index = index/HASHMAP_BITMAP_TYPE_SIZE;
  size_t ib_offset = index%HASHMAP_BITMAP_TYPE_SIZE;
  size_t ffs_offset = 0;

  // get bitmap entry
  // right shift to get rid of old 1 bits we don't care about
  bitmap_t ib = item_bitmap[ib_index] >> ib_offset;

  while(index < size)
  {
    // find first set bit using ffs
    ffs_offset = __pony_ffsl(ib);

    // if no bits set; increment index to next item bitmap entry
    if(ffs_offset == 0)
    {
      index += (HASHMAP_BITMAP_TYPE_SIZE - ib_offset);
      ib_index++;
      ib_offset = 0;
      ib = item_bitmap[ib_index];
      continue;
    } else {
      // found a set bit for valid element
      index += (ffs_offset - 1);

      // no need to check if valid element because item bitmap keeps track of it
      *i = index;
      return buckets[index];
    }
  }

  // searched through bitmap and didn't find any more valid elements.
  // index could be bigger than size due to use of ffs
  *i = size;
  return NULL;
}

size_t ponyint_hashmap_size(hashmap_t* map)
{
  return map->count;
}

void* ponyint_hashmap_clearindex(hashmap_t* map, size_t index)
{
  if(map->size <= index)
    return NULL;

  void* elem = map->buckets[index];

  if(!valid(elem))
    return NULL;

  map->buckets[index] = NULL;
  map->count--;

  // update item bitmap
  size_t ib_index = index/HASHMAP_BITMAP_TYPE_SIZE;
  size_t ib_offset = index%HASHMAP_BITMAP_TYPE_SIZE;
  map->item_bitmap[ib_index] &= ~((bitmap_t)1 << ib_offset);

  return elem;
}

void ponyint_hashmap_optimize(hashmap_t* map, hash_fn hash, alloc_fn alloc,
  free_size_fn fr, cmp_fn cmp)
{
  size_t count = 0;
  size_t i = HASHMAP_BEGIN;
  void* elem;

  do
  {
    count = 0;
    i = HASHMAP_BEGIN;
    while((elem = ponyint_hashmap_next(&i, map->count, map->item_bitmap,
      map->size, map->buckets)) != NULL)
    {
      count += optimize_item(map, hash, alloc, fr, cmp, i, elem);
    }
  } while(count > 0);
}
