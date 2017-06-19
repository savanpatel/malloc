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


#include "malloc.h"

/*
  Aligns pointer to 8 byte address.
  params : pointer to align.
  returns: a pointer (aligned to 8 bytes)
 */
void *align8(void *x)
{
    unsigned long p = (unsigned long)x;
    p = (((p - 1) >> 3) << 3) + 8;
    return (void *)p;
}


/*
 *  returns the pointer to elated bin based on the size.
 *  params: size of bin.
 *  returns: pointer to bin corresponding size.
 */
block_info** get_bin(size_t size)
{
    switch(size)
    {
       case 8   : return &bin_8;
       case 64  : return &bin_64;
       case 512 : return &bin_512;
       default  : return &bin_large;
    }
}


/*
 *  Creates a memory block from unused heap.
 *  params: requested memory size in bytes.
 *  returns: pointer to allocated memory chunk. NULL on failure.
 */
void * block_from_unused_heap(size_t size)
{
    /*If thread heap is not initialized or if available free size is less
      than the block for requested size.*/
    if(NULL == thread_unused_heap_start ||
       (thread_heap_end - thread_unused_heap_start) <
           (size + sizeof(block_info)))
    {

        /*If heap of process is not initialized or available free size of general
          heap is less than the block for requested size.*/
        if(NULL == heap_used_memory_end ||
            (sbrk(0) - heap_used_memory_end) < (size + sizeof(block_info)))
        {
            /*If heap is not initialized.*/
            if(NULL == heap_used_memory_end)
            {
                heap_used_memory_end = sbrk(0);
                if(heap_used_memory_end == (void*) -1)
                {
                    errno = ENOMEM;
                    perror("\n sbrk(0) failed.");
                    return NULL;
                }
            }

            align8(heap_used_memory_end);
        }

        // extend heap, return NULL on failure.
        if(sbrk(sysconf(_SC_PAGESIZE) * 100) == (void *) -1)
        {
            errno = ENOMEM;
            perror("\n sbrk failed to extend heap.");
            return NULL;
        }

        /*If there is smaller chunk remaining, add to free list of a bin.
          to minimize the wastage of memory.*/
        /*if(NULL != thread_unused_heap_start)
        {
            // TODO: add_chunk_to_bin(); possible optimization.
        }*/

        /*create fresh heap of 1 page size. for a thread.*/
        thread_unused_heap_start = heap_used_memory_end;
        thread_heap_end =
            heap_used_memory_end + (sysconf(_SC_PAGESIZE));
        heap_used_memory_end =  thread_heap_end;
    }

    block_info b;
    b.size = size;
    b.next = NULL;

    memcpy(thread_unused_heap_start, &b, sizeof(block_info));
    thread_unused_heap_start += (sizeof(block_info) + size);

    // update stats variables.
    pthread_mutex_lock(&stats_mutex);
    total_number_of_blocks++;
    total_arena_size_allocated += size;
    pthread_mutex_unlock(&stats_mutex);

    return (thread_unused_heap_start - size);
}




/*
 * Allocate memory from heap area. For memory request of sizes < 512, chunks are
 * allocated from heap.
 * params : size.
 * returns: pointer to allocated area.
 */
void *heap_allocate(size_t size)
{

   block_info **bin = get_bin(size);
   void * ret = NULL;
   /* reuse memory block from heap bins if available*/
   if(NULL != *bin)
   {
       block_info *p = *bin;
       *bin =  p->next;
       p->next = NULL;

       pthread_mutex_lock(&stats_mutex);
       total_free_blocks--;
       pthread_mutex_unlock(&stats_mutex);
       ret = (void *)((char*)p + sizeof(block_info));
   }
   else  //request new memory or slice out remaining unused memory.
   {     pthread_mutex_lock(&global_heap_mutex);
         ret =  block_from_unused_heap(size);
         pthread_mutex_unlock(&global_heap_mutex);
   }

   return ret;
}



/*
  Finds best fitting block from large memory bin.
*/
void * find_best_fit_from_bin_large(size_t size)
{
    block_info *b = bin_large;
    block_info *best_fit = NULL;
    int min_fit_size = INT_MAX;
    void *ret = NULL;

    while(b != NULL)
    {
         if(b->size >= size && b->size < min_fit_size)
         {
            best_fit = b;
            min_fit_size = b->size;
         }
         b = b->next;
    }

    /*If best fit found, update list*/
    if(NULL != best_fit)
    {
        // if best_fit is first block.
        if (best_fit == bin_large)
        {
           bin_large = bin_large->next;
           best_fit->next = NULL;
           ret = (void *)((void *)best_fit + sizeof(block_info));
        }
        else
        {
          b = bin_large;
          while(b != NULL && b->next != best_fit)
          {
            b = b->next;
          }
          if(b != NULL)
          {
             b->next = best_fit->next;
          }
          best_fit->next = NULL;
          ret = (void *)((void *)best_fit + sizeof(block_info));
        }
    }

    return ret;
}




/*
 * maps new memory address using mmap system call for size
 * request > 512 bytes.
 * Requests kernel to map new memory at some place decided by kernel.
 * params: requested size in bytes.
 * returns: pointer to block allocated., NULL on failure.
 */
void * mmap_new_memory(size_t size)
{
    int num_pages =
        ((size + sizeof(block_info) - 1)/sysconf(_SC_PAGESIZE)) + 1;
    int required_page_size = sysconf(_SC_PAGESIZE) * num_pages;

    void *ret = mmap(NULL, // let kernel decide.
                     required_page_size,
                     PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS| MAP_PRIVATE,
                     -1, //no file descriptor
                     0); //offset.

    block_info b;
    b.size = (required_page_size - sizeof(block_info));
    b.next = NULL;

    ret = memcpy(ret, &b, sizeof(block_info));
    ret = ((char*)ret + sizeof(block_info));

    // update stats variables.
    pthread_mutex_lock(&stats_mutex);
    total_mmap_size_allocated += size;
    pthread_mutex_unlock(&stats_mutex);

    return ret;
}


/*
 * Performs allocation for request > 512 bytes.
 * params : requested memory size.
 * returns: pointer to allocated memory. NULL on failure.
 */
void *alloc_large(size_t size)
{
   void * ret = NULL;
   if(NULL != bin_large)
   {
       //pthread_mutex_lock(&global_heap_mutex);
       ret = find_best_fit_from_bin_large(size);
      // pthread_mutex_unlock(&global_heap_mutex);
   }

   /*either bin_large is empty or no best fit was found.*/
   if(ret == NULL)
   {
       //pthread_mutex_lock(&global_heap_mutex);
       ret = mmap_new_memory(size);
      // pthread_mutex_unlock(&global_heap_mutex);
   }
    return ret;
}


/*
 * Allocates the memory.
 */
void* malloc(size_t size)
{
     pthread_mutex_lock(&stats_mutex);
     total_allocation_request++;
     pthread_mutex_unlock(&stats_mutex);

     void * ret = NULL;

     if(size < 0)
     {
        perror("\n Invalid memory request.");
        return NULL;
     }

     // allocate from either large bin or mmap.
     if(size > 512)
     {  //printf("Alloc large\n");
        ret = alloc_large(size);
     }
     else
     {
       size = (size <= 8)? 8 : ((size<=64)? 64: 512);
       ret = heap_allocate(size);
     }
     return ret;
}



/*
 * Free up the memory allocated at pointer p. It appends the block into free
 * list.
 * params: address to pointer to be freed.
 * returns: NONE.
 */
void free(void *p)
{
   //update stats variables.
   pthread_mutex_lock(&stats_mutex);
   total_free_request++;
   total_free_blocks++;
   pthread_mutex_unlock(&stats_mutex);

   if(NULL != p)
   {
      block_info *block  = (block_info *)(p - sizeof(block_info));
      memset(p, '\0', block->size);

      block_info **bin = get_bin(block->size);
      block_info *check_bin = *bin;

      // already freed?
      while(check_bin != NULL)
      {
         if(check_bin == block)
         {
            return;
         }
         check_bin = check_bin->next;
      }

      // attach as head to free list of corresponding bin.
      block->next = *bin;
      *bin = block;
    }
}


/*similar to calloc of glibc */
void *calloc(size_t nmemb, size_t size)
{
     void *p = malloc(nmemb * size);
     block_info *b = (block_info *)(p - sizeof(block_info));
     memset(p, '\0', b->size);
     return p;
}




/*
 * Allocate size of size bytes for nmemb. Initialize with null bytes.
 * params: total number of elements of size 'size' to be allocated.
 *         and size to allocate.
 * returns: pointer to allocated memory on success. NULL on failure.
 */void *realloc(void *ptr, size_t size)
{
    if(NULL == ptr)
    {
       return malloc(size);
    }
    void *newptr = malloc(size);

    if(NULL == newptr)
    {
        return NULL;
    }
    block_info *old_block =
        (block_info *)((char*)ptr - sizeof(block_info));

    memcpy(newptr, ptr, old_block->size);

    free(ptr);

    return newptr;
}


/*
 * Fork hook. This will be called before fork happens.
 * The method holds lock so as to make sure none of the active threads
 * are holding lock and thus making sure, none of the threads are
 * in middle of malloc process.
 * later child hooks can reset the mutex to initial value.
 */
void prep_fork(void)
{
    // take lock before fork so as to make sure no other thread is
    // holding lock.
    pthread_mutex_lock(&global_heap_mutex);
}


/*
 * Since mutex is held by prep_fork method, it can safely be reset
 * in parent process.
 */
void parent_fork_handle(void)
{
  pthread_mutex_init(&global_heap_mutex, NULL);
}


/*
 * Since mutex is held by prep_fork method, it can safely be reset
 * in child process.
 */
void child_fork_handle(void)
{
   pthread_mutex_init(&global_heap_mutex, NULL);
}



/*
 * Global constructor to Initialize the fork hooks
 */
__attribute__((constructor)) void sharedLibConstructor(void)
{
  int ret =
      pthread_atfork(&prep_fork,
                     &parent_fork_handle,
                     &child_fork_handle);
  if (ret != 0)
  {
      perror("pthread_atfork() error [Call #1]. Malloc is now not fork safe.");
  }

  /*if(mcheck(NULL) != 0)
  {  TODO: mcheck implemtation.
      perror("\n mcheck failed");
  }*/

}


void *memalign(size_t alignment, size_t s)
{
    return heap_used_memory_end;
}


void malloc_stats()
{
    printf("\n -- malloc stats--\n");
    printf("\n total_arena_size_allocated : %lu", total_arena_size_allocated);
    printf("\n total_mmap_size_allocated  : %lu", total_mmap_size_allocated);
    printf("\n total_number_of_blocks     : %lu", total_number_of_blocks);
    printf("\n total_allocation_request   : %lu", total_allocation_request);
    printf("\n total_free_request         : %lu", total_free_request);
    printf("\n total_free_blocks          : %lu\n", total_free_blocks);
}
