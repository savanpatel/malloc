# malloc
Thread safe and fork safe malloc library.


-------------------------------------------------------------------------------
  1 INTRODUCTION
-------------------------------------------------------------------------------

This file discusses usage, design choices, issues and future works
of a malloc library. This malloc library is not as efficient as 
GCC's malloc library, but it provides malloc, free, calloc and 
realloc functionality in multi threading and fork safe envrironment.

Author: Savan Patel
Report Bug/Issue at patel.sav@husky.neu.edu




-------------------------------------------------------------------------------
  2 USAGE
-------------------------------------------------------------------------------
  2.1 Running sample test
     
      type in commad on terminal: make
        Makefile by default will build a sharedlib from malloc.c with name
        libmalloc.so. It then runs sample test program using this shared library
        to call malloc() and free().
  
  2.2 General usage
      To avoid loading of malloc library of gcc by default, use LD_PRELOAD
      to preload this malloc library. (see Makefile for example)
      Example :  
            LD_PRELOAD= `pwd`/libmalloc.so ./test
      
      ** Make sure to compile your program with -pthread flag


-------------------------------------------------------------------------------
  3 DESIGN CHOICES
-------------------------------------------------------------------------------
  3.1 Code Structure
      
      malloc.h : Contains basic declaration of library and helper functions
                 to manage heap. 
                 
                 block_info: structure that stores memory
                             info of free block.
                 
                 One global void pointer for pointing first unused address 
                 of entire process.
                
                 Four threadlocal memory bins (free list) of size 8, 64, 512 and
                 > 512 bytes. Bins are linked list of type block_info.
                 
                 malloc_stats() to print malloc statastics.
 
      malloc.c : Implements malloc(), free(), calloc() and realloc().
                 Allocates per thread arenas from global heap.
                 
      
  3.2 Design and Implementation
      
      3.2.1  Implementing malloc
                1) When malloc() is called, firs the repsective bin is looked 
                   based on the requested size. If a block is available in free
                   list, it is removed from head and the pointer to memory 
                   address of block is returned.
                
                2) If it were very first call (of entire process),
                    1) Heap is created by extending memory using sbrk() syscall
                    2) A portion of memory is allocated from global heap
                       to current thread.
                    3) Now thread can slice out blocks of 8, 64 or 512 bytes
                       from allocated memory.
                       ** Note: Actual block size = 
                                   bin_size + sizeof(block_info)
                    4) A typical allocated memory block looks like
                           
                                     ------------------------------
                                    | block_info| user    memory  |
                                     ------------------------------

               3) If it is very first call for a thread, 
                  1) A heap is allocated from the global heap.
                     ** Global heap is extended if its out of free memory.
                  2) Thread now has its own memory heap to allocate memory.
                  3) library uses mutex to avoid contention in getting 
                     per thread memory from global heap area.

               4) In case sbrk fails, errono ENOMEM is set and NULL is returned.

               5) For request of allocation of size > 512 bytes,
                  the memory is mapped via mmap syscall with kernel deciding
                  address space.
             
                  Code snippet:
                         void *ret = mmap(NULL, // let kernel decide.
                                          required_page_size,
                                          PROT_READ | PROT_WRITE,
                                          MAP_ANONYMOUS| MAP_PRIVATE,
                                          -1, //no file descriptor
                                           0); //offset.

                  ** Before mmap bin_large is checked for best fit block if
                     available.

             THREAD SAFETY
                 The implementation is thread safe in manner:
                 1) Each thread is allocated its own thread heap arena.
                 2) Free list are thread locals i.e each thread has its own 4 free
                    list.
                 3) library uses mutex while requesting per thread heap from 
                    process's global heap memory area.


             FORK SAFETY
                 This malloc library through global constructor registers
                 three methods using pthread_at_fork() to handle fork and
                 lock inconsistency.
                 The working is as follows
                 
                 Upon fork,
                 1) prep_fork() is called just before fork. This method tries to aquire
                    lock on `global_heap_mutex`. Upon successful heap lock it would mean
                    no other thread is holding lock and thus in middle of malloc() call.

                 2) Now fork happens and the methods parent_fork_handle() and
                    child_fork_handle() are called post successful fork. Here,
                    mutex can safely be rest to initial value and process/threads can
                    continue.
                 
                 code snippet:
                 -------------------------------------------------------------------
               
                  /* hold lock before fork*/
                  void prep_fork(void)                                              
                  {                                                                 
                     pthread_mutex_lock(&global_heap_mutex);                        
                  }                                                                 
                 
                 

                  /* reset/reinitialize in parent*/

                  void parent_fork_handle(void)
                  {
                     pthread_mutex_init(&global_heap_mutex, NULL);
                  }
                 


                  /* reinitialize in child*/

                  void child_fork_handle(void)
                  {
                    pthread_mutex_init(&global_heap_mutex, NULL);
                  }
                 
                  ------------------------------------------------------------------
             
    
      3.2.2  free
      
             1) When free is called for pointer p, 
                the size of block is determined from the
                block info at address just before p (p - sizeof(block_info)).
                This free block is now added to head of respective free bin list.
                
                Memory of pointer p is cleared before adding to list.
      
             2) Before attaching to free bin list, it is checked if block is already
                in free list. (yes would mean free is called twice). In this case
                the function returns immediately.

      3.2.3  calloc
             1) calloc(size_t nmemb, size_t size) simply calls malloc with size
                nmemb * size
             2) It then memsets entire block with null value and returns a pointer.
           
      3.2.4 realloc
            1) realloc(void *ptr, size_t size) first request a new memory area
               using malloc().
            2) Upon successful, it copies old data to new memory and frees up old 
               pointer.
            3) Upon FAILURE, realloc returns NULL but old pointer is not freed up.
               user still can use old memory chunk.
     


-------------------------------------------------------------------------------
  4 PERFORMANCE
-------------------------------------------------------------------------------
    Below is a sample performance comparison of malloc library against standard
    gcc malloc. Although the library is gratly slow, it runs wihtout break
    in heavy multithreading environment. Possible performance optimizations
    are discussed in section 5.


    HARDWARE SPECS
    ------------- 
    CPU   : Core i7 6th Gen 6500U 3.1 GHz
    Memory: 8GB
    OS    :  ubuntu 16.04 LTS


    This malloc library

	Using posix threads.
	total=500 threads=2 i_max=10000 size=10000 bins=3355
	Created thread 7f083de4c700.
	Created thread 7f083d64b700.
	n_total = 50
	n_total = 100
	n_total = 150
	n_total = 200
	n_total = 250
	n_total = 300
	n_total = 350
	n_total = 400
	n_total = 450
	n_total = 500
	
        
        real	0m31.894s
	user	0m58.784s
	sys	0m3.296s
    

   GCC Malloc
      Using posix threads.
	total=500 threads=2 i_max=10000 size=10000 bins=3355
	Created thread 7f14a59e0700.
	Created thread 7f14a51df700.
	n_total = 50
	n_total = 100
	n_total = 150
	n_total = 200
	n_total = 250
	n_total = 300
	n_total = 350
	n_total = 400
	n_total = 450
	n_total = 500
	
	real	0m1.083s
	user	0m0.628s
	sys	0m0.648s




-------------------------------------------------------------------------------
  5 KNOWN ISSUES/BUGS/ FUTURE WORK
-------------------------------------------------------------------------------
  
  5.1 Memory optimization: 
        Wastage of a memory upto 512 bytes happens in following case.
        If thread, after many allocation has remaining heap size 512 bytes
        and now a request for allocation of 512 bytes is made,
        1) Since the available size is less  than 512 + sizeof(block_info)
           a new heap meory will be allocated for this thread with updated
           start and end pointers. 
           The memory chunk of 512 bytes(in worst case) will remain unused
           throughout.

        POSSIBLE SOLUTION:
           In such case, break the remaining chunk into sizes of 8/16 bytes 
           and append to free list. This would mean least wastage (< 1-2%).
           
  5.2 Slow malloc
         Current implementation is greatly slower than standard gcc malloc.
         
        POSSIBLE OPTIMIZATIONS:
         1. Iniialize the bins with free list at the time of first malloc
            this would mean faster availability of blocks in future malloc calls.
         
         2. Implement lock free data structures while manipulating global heap.
          
         3. Using futex to improve locking performance. Implementation details 
            are yet to be determined. But idea is to replace mutex locking with
            futexes that would improve performance of locking in multi threaded env.
        
            
        
------------------------------------------------------------------------------------    
