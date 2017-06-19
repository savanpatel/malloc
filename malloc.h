/*
 * Implements malloc library in C.
 * This malloc implemtation has per thread bins.
 * bins are according to four sizes:
 * 8 bytes, 64 bytes, 512 bytes and greated than 512 bytes.
 *
 * memory for size > 512 bytes is allocated through mmap system call.
 * memory blocks are initialized lazily and are appended on free list after
 * free() call.
 *
 * Author: Savan Patel
 * Email : patel.sav@husky.neu.edu
 *
 */

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <mcheck.h>

#ifndef _MALLOC_H
#define _MALLOC_H 1


/* struct to hold block metadata
 * size represents the free block's size in bytes.
 * next points to next free block.
 */
typedef struct block_info
{
   int size;
   struct block_info *next;
}block_info;

/*mutex for global heap.*/
pthread_mutex_t global_heap_mutex = PTHREAD_MUTEX_INITIALIZER;

/* mutex for updating the stats variables */
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;


// total arena size allocated in bytes.
unsigned long total_arena_size_allocated = 0;

// total size allocated through mmap system call.
unsigned long total_mmap_size_allocated = 0;

// total number of blocks in heap.
unsigned long total_number_of_blocks = 0;

// total number of allocation done. It is count of number of times malloc called
unsigned long total_allocation_request = 0;

// total free requests made.
unsigned long total_free_request = 0;

// total number of free blocks available (of all threads.)
unsigned long total_free_blocks = 0;


/* Four bins of size 8 byte, 64 byte, 512 byte and every thing else greater
 *  than 512 bytes.
 *
 * Each thread will have its own bin and storage arena.
 * Iniially all the bins are empty. The list gets build up on successive free
 * calls after malloc.
 */
__thread block_info *bin_8     = NULL;
__thread block_info *bin_64    = NULL;
__thread block_info *bin_512   = NULL;
__thread block_info *bin_large = NULL;


/*
 * A pointer to heap memory upto which the heap addresses are assigned to
 * the threads of process. Addresses beyond this upto heap end are available
 * for future thread of more memory expansion to threads.
 */
void *heap_used_memory_end = NULL;

/*
 *  pointer to a location from which hepa memory allocated to thread has not
 *  been
 *  used yet.
 */
__thread void *thread_unused_heap_start = NULL;


/*
 * End point for the thread heap memory area.
 */
__thread void *thread_heap_end = NULL;



/*
  Aligns pointer to 8 byte address.
  params : pointer to align.
  returns: a pointer (aligned to 8 bytes)
 */
void * align8(void *x);




/*
 *  returns the pointer to elated bin based on the size.
 *  params: size of bin.
 *  returns: pointer to bin corresponding size.
 */
block_info ** get_bin(size_t size);




/*
 * Allocate memory from heap area. For memory request of sizes < 512, chunks are
 * allocated from heap.
 * params : size.
 * returns: pointer to allocated area.
 */
void * heap_allocate(size_t size);




/*
 * Finds best fit block from bin_large. On memory request > 512, first
 * it is checked if any of large free memory chunks fits to request.
 *
 * params: size of block to allocate.
 * returns: pointer to best fitting block, NULL on failure.
 */
void * find_best_fit_from_bin_large(size_t size);



/*
 * maps new memory address using mmap system call for size request > 512 bytes.
 * params: requested size in bytes.
 * returns: pointer to block allocated., NULL on failure.
 */
void * mmap_new_memory(size_t size);



/*
 * Performs allocation for request > 512 bytes.
 * params : requested memory size.
 * returns: pointer to allocated memory. NULL on failure.
 */
void * alloc_large(size_t size);




/*
 *  Creates a memory block from unused heap.
 *  params: requested memory size in bytes.
 *  returns: pointer to allocated memory chunk. NULL on failure.
 */
void * block_from_unused_heap(size_t size);




/*
 * Allocates the memory.
 */
void * malloc(size_t);




/*
 * Free up the memory allocated at pointer p. It appends the block into free
 * list.
 * params: address to pointer to be freed.
 * returns: NONE.
 */
void free(void *p);




/*
 * Allocate size of size bytes for nmemb. Initialize with null bytes.
 * params: total number of elements of size 'size' to be allocated. and size
 *         to allocate.
 * returns: pointer to allocated memory on success. NULL on failure.
 */
void * calloc(size_t nmemb, size_t size);




/*
 * reallocates the pointer with new size size and copies the old data to new
 * location.
 * params: pointer to reallocate. and new size.
 * returns: pointer to new allocated memory chunk.
 */
void * realloc(void *ptr, size_t size);




/* aligns memory.
 */
void * memalign(size_t alignment, size_t s);




/*
 * Prints malloc stats like number of free blocks, total number of memory
 * allocated.
 */
void malloc_stats();

void abortfn(enum mcheck_status status);
#endif
