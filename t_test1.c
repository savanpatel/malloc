/*
 * $Id: t-test1.c,v 1.1.1.1 2003/07/02 16:32:09 matthias.urban Exp $
 * by Wolfram Gloger 1996-1999
 * A multi-thread test for malloc performance, maintaining one pool of
 * allocated bins per thread.
 *
 * Fixed condition variable usage, and ported to windows
 * Steven Fuerst 2009
 */

#define USE_PTHREADS	1
#define USE_MALLOC	0
#define USE_SPROC	0
#define USE_THR		0

/* Testing level */
#ifndef TEST
#define TEST 0
#endif

#define N_TOTAL		500
#ifndef N_THREADS
#define N_THREADS	2
#endif
#ifndef N_TOTAL_PRINT
#define N_TOTAL_PRINT 50
#endif
#define STACKSIZE	32768
#ifndef MEMORY
#define MEMORY		(1ULL << 26)
#endif

#define RANDOM(s)	(rng() % (s))

#define MSIZE		10000
#define I_MAX		10000
#define ACTIONS_MAX	30


#ifndef __GNUC__

#include "malloc.c"
#include <windows.h>
#include <intrin.h>
#pragma warning(disable:4146 4244)

#define inline __inline
#define __thread __declspec(thread)

struct _pthread_v
{
	void *arg;
	void *(* func)(void *);
	void *retval;
	HANDLE h;
};

typedef struct _pthread_v *pthread_t;

static int pthread_create_wrapper(void *args)
{
	struct _pthread_v *tv = args;

	/* Call function and save return value */
	tv->retval = tv->func(tv->arg);

	return 0;
}

/* Visual Studio miscompiles this if it is inlined */
static int pthread_create(pthread_t *th, void *attr, void *(* func)(void *), void *arg)
{
	struct _pthread_v *tv = calloc(1, sizeof(struct _pthread_v));

	/* Ignore attributes for now */
	(void) attr;

	if (!tv) return 1;

	*th = tv;

	/* Save data in pthread_t */
	tv->arg = arg;
	tv->func = func;

	tv->h = (HANDLE) _beginthreadex(NULL, 0, pthread_create_wrapper, tv, 0, NULL);

	return !tv->h;
}

static int pthread_join(pthread_t t, void **res)
{
	struct _pthread_v *tv = t;

	WaitForSingleObject(tv->h, INFINITE);
	CloseHandle(tv->h);

	/* Obtain return value */
	if (res)
	{
		/* Hack - get correct return value, not the copy passed to us */
		*res = tv->retval;
	}

	free(tv);

	return 0;
}

#define pthread_mutex_lock EnterCriticalSection
#define pthread_mutex_unlock LeaveCriticalSection
#define pthread_mutex_trylock(L) (TryEnterCriticalSection(L) + EBUSY)
#define pthread_mutex_t CRITICAL_SECTION
#define pthread_mutex_init(L, A) InitializeCriticalSection(L)
#define pthread_mutex_destroy(L) DeleteCriticalSection(L)
#define pthread_cond_init(C, A) InitializeConditionVariable(C)
#define pthread_cond_signal(C) WakeConditionVariable(C)
#define pthread_cond_broadcast(C) WakeAllConditionVariable(C)
#define pthread_cond_wait(C, M) SleepConditionVariableCS((C), (M), INFINITE)
#define pthread_cond_t CONDITION_VARIABLE

/* Hack - replace memalign with malloc, so we can compile */
#define memalign(A, S) malloc(S)

#else

#ifdef USE_PTHREADS /* Posix threads */

#include <pthread.h>

#elif USE_THR /* Solaris threads */

#include <thread.h>
typedef thread_t pthread_t;

#elif USE_SPROC /* SGI sproc() threads */

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <abi_mutex.h>

typedef int pthread_t;

#else /* no USE_... are defined */

#define NO_THREADS
typedef int pthread_t;

#endif /* defined(_LIBC) */

#endif /* __GCC__ */

#if (defined __STDC__ && __STDC__) || defined __cplusplus
# include <stdlib.h>
#endif
#include <stdio.h>

#ifdef __GCC__
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#endif

#include <sys/types.h>
#include <malloc.h>


/*
 * Ultra-fast RNG: Use a fast hash of integers.
 * 2**64 Period.
 * Passes Diehard and TestU01 at maximum settings
 */
static __thread unsigned long long rnd_seed;

static inline unsigned rng(void)
{
	unsigned long long c = 7319936632422683443ULL;
	unsigned long long x = (rnd_seed += c);

	x ^= x >> 32;
	x *= c;
	x ^= x >> 32;
	x *= c;
	x ^= x >> 32;

	/* Return lower 32bits */
	return x;
}

/* For large allocation sizes, the time required by copying in
   realloc() can dwarf all other execution times.  Avoid this with a
   size threshold. */
#ifndef REALLOC_MAX
#define REALLOC_MAX	2000
#endif

struct bin
{
	unsigned char *ptr;
	size_t size;
};

#if USE_PTHREADS /* Posix threads */

static pthread_cond_t finish_cond;
static pthread_mutex_t finish_mutex;

#endif

#if TEST > 0

static void mem_init(unsigned char *ptr, size_t size)
{
	size_t i, j;

	if (!size) return;
	for (i = 0; i < size; i += 2047)
	{
		j = (size_t)ptr ^ i;
		ptr[i] = j ^ (j>>8);
	}
	j = (size_t)ptr ^ (size - 1);
	ptr[size-1] = j ^ (j>>8);
}

static int mem_check(unsigned char *ptr, size_t size)
{
	size_t i, j;

	if (!size) return 0;
	for (i = 0; i < size; i += 2047)
	{
		j = (size_t)ptr ^ i;
		if (ptr[i] != ((j ^ (j>>8)) & 0xFF)) return 1;
	}
	j = (size_t)ptr ^ (size - 1);
	if (ptr[size-1] != ((j ^ (j>>8)) & 0xFF)) return 2;
	return 0;
}

static int zero_check(void *p, size_t size)
{
	unsigned *ptr = p;
	unsigned char *ptr2;

	while (size >= sizeof(*ptr))
	{
		if (*ptr++) return -1;
		size -= sizeof(*ptr);
	}
	ptr2 = (unsigned char*)ptr;

	while (size > 0)
	{
		if (*ptr2++) return -1;
		--size;
	}
	return 0;
}

#endif /* TEST > 0 */

/*
 * Allocate a bin with malloc(), realloc() or memalign().
 * r must be a random number >= 1024.
 */
static void bin_alloc(struct bin *m, size_t size, unsigned r)
{
#if TEST > 0
	if (mem_check(m->ptr, m->size))
	{
		printf("memory corrupt!\n");
		exit(1);
	}
#endif
	r %= 1024;

	if (r < 4)
	{
		/* memalign */
		if (m->size > 0) free(m->ptr);
		m->ptr = memalign(sizeof(int) << r, size);
	}
	else if (r < 20)
	{
		/* calloc */
		if (m->size > 0) free(m->ptr);
		m->ptr = calloc(size, 1);
#if TEST > 0
		if (zero_check(m->ptr, size))
		{
			size_t i;
			for (i = 0; i < size; i++)
			{
				if (m->ptr[i]) break;
			}
			printf("calloc'ed memory non-zero (ptr=%p, i=%ld)!\n", m->ptr, i);
			exit(1);
		}
#endif
	}
	else if ((r < 100) && (m->size < REALLOC_MAX))
	{
		/* realloc */
		if (!m->size) m->ptr = NULL;
		m->ptr = realloc(m->ptr, size);
	}
	else
	{
		/* malloc */
		if (m->size > 0) free(m->ptr);
		m->ptr = malloc(size);
	}
	if (!m->ptr)
	{
		printf("out of memory (r=%d, size=%ld)!\n", r, (unsigned long)size);
		exit(1);
	}

	m->size = size;
#if TEST > 0
	mem_init(m->ptr, m->size);
#endif
}

/* Free a bin. */

static void bin_free(struct bin *m)
{
	if (!m->size) return;

#if TEST > 0
	if (mem_check(m->ptr, m->size))
	{
		printf("memory corrupt!\n");
		exit(1);
	}
#endif

	free(m->ptr);
	m->size = 0;
}

struct bin_info
{
	struct bin *m;
	size_t size, bins;
};

#if TEST > 0
static void bin_test(struct bin_info *p)
{
	int b;

	for (b = 0; b < p->bins; b++)
	{
		if (mem_check(p->m[b].ptr, p->m[b].size))
		{
			printf("memory corrupt!\n");
			exit(1);
		}
	}
}
#endif

struct thread_st
{
	int bins, max, flags;
	size_t size;
	pthread_t id;
	char *sp;
	size_t seed;
};

#if USE_PTHREADS || defined USE_THR || defined NO_THREADS
static void *malloc_test(void *ptr)
#else
static void malloc_test(void *ptr, size_t stack_len)
#endif
{
	struct thread_st *st = ptr;
	int i, pid = 1;
	unsigned b, j, actions;
	struct bin_info p;

	rnd_seed = st->seed;

#ifdef TEST_FORK
	if (!RANDOM(TEST_FORK))
	{
		int status;

		printf("forking\n");
		pid = fork();
		if (pid > 0)
		{
			printf("waiting for %d...\n", pid);
			waitpid(pid, &status, 0);
			if (!WIFEXITED(status))
			{
				printf("child term with signal %d\n", WTERMSIG(status));
			}
			goto end;
		}
	}
#endif

	p.m = malloc(st->bins * sizeof(*p.m));
	p.bins = st->bins;
	p.size = st->size;
	for (b = 0; b < p.bins; b++)
	{
		p.m[b].size = 0;
		p.m[b].ptr = NULL;
		if (!RANDOM(2)) bin_alloc(&p.m[b], RANDOM(p.size) + 1, rng());
	}

	for (i = 0; i <= st->max;)
	{
#if TEST > 1
		bin_test(&p);
#endif
		actions = RANDOM(ACTIONS_MAX);

#if USE_MALLOC && MALLOC_DEBUG
		if (actions < 2) mallinfo();
#endif
		for (j = 0; j < actions; j++)
		{
			b = RANDOM(p.bins);
			bin_free(&p.m[b]);
		}
		i += actions;
		actions = RANDOM(ACTIONS_MAX);

		for (j = 0; j < actions; j++)
		{
			b = RANDOM(p.bins);
			bin_alloc(&p.m[b], RANDOM(p.size) + 1, rng());
#if TEST > 2
			bin_test(&p);
#endif
		}

		i += actions;
	}

	for (b = 0; b < p.bins; b++) bin_free(&p.m[b]);

	free(p.m);

#ifdef TEST_FORK
end:
#endif
#if USE_PTHREADS
	if (pid > 0)
	{
		pthread_mutex_lock(&finish_mutex);
		st->flags = 1;
		pthread_cond_signal(&finish_cond);
		pthread_mutex_unlock(&finish_mutex);
	}
#elif USE_SPROC
	return;
#endif
	return NULL;
}

static int my_start_thread(struct thread_st *st)
{
#if USE_PTHREADS
	pthread_create(&st->id, NULL, malloc_test, st);
#elif USE_THR
	if (!st->sp) st->sp = malloc(STACKSIZE);
	if (!st->sp) return -1;
	thr_create(st->sp, STACKSIZE, malloc_test, st, THR_NEW_LWP, &st->id);
#elif USE_SPROC
	if (!st->sp) st->sp = malloc(STACKSIZE);
	if (!st->sp) return -1;
	st->id = sprocsp(malloc_test, PR_SALL, st, st->sp+STACKSIZE, STACKSIZE);
	if (st->id < 0) return -1;
#else /* NO_THREADS */
	st->id = 1;
	malloc_test(st);
#endif
	return 0;
}

static int n_total = 0;
static int n_total_max = N_TOTAL;
static int n_running;

static int my_end_thread(struct thread_st *st)
{
	/* Thread st has finished.  Start a new one. */
	if (n_total >= n_total_max)
	{
		n_running--;
	}
	else if (st->seed++, my_start_thread(st))
	{
		printf("Creating thread #%d failed.\n", n_total);
	}
	else
	{
		n_total++;
		if (!(n_total%N_TOTAL_PRINT)) printf("n_total = %d\n", n_total);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int i, bins;
	int n_thr = N_THREADS;
	int i_max = I_MAX;
	size_t size = MSIZE;
	struct thread_st *st;

	if (argc > 1) n_total_max = atoi(argv[1]);
	if (n_total_max < 1) n_thr = 1;
	if (argc > 2) n_thr = atoi(argv[2]);
	if (n_thr < 1) n_thr = 1;
	if (n_thr > 100) n_thr = 100;
	if (argc > 3) i_max = atoi(argv[3]);

	if (argc > 4) size = atol(argv[4]);
	if (size < 2) size = 2;

	bins = MEMORY  /(size * n_thr);
	if (argc > 5) bins = atoi(argv[5]);
	if (bins < 4) bins = 4;

#if USE_PTHREADS
	printf("Using posix threads.\n");
	pthread_cond_init(&finish_cond, NULL);
	pthread_mutex_init(&finish_mutex, NULL);
#elif USE_THR
	printf("Using Solaris threads.\n");
#elif USE_SPROC
	printf("Using sproc() threads.\n");
#else
	printf("No threads.\n");
#endif

	printf("total=%d threads=%d i_max=%d size=%ld bins=%d\n",
		   n_total_max, n_thr, i_max, size, bins);

	st = malloc(n_thr * sizeof(*st));
	if (!st) exit(-1);

#if !defined NO_THREADS && defined __sun__
	/* I know of no other way to achieve proper concurrency with Solaris. */
	thr_setconcurrency(n_thr);
#endif
#if USE_PTHREADS
	pthread_mutex_lock(&finish_mutex);
#endif

	/* Start all n_thr threads. */
	for (i = 0; i < n_thr; i++)
	{
		st[i].bins = bins;
		st[i].max = i_max;
		st[i].size = size;
		st[i].flags = 0;
		st[i].sp = 0;
		st[i].seed = (i_max * size + i) ^ bins;
		if (my_start_thread(&st[i]))
		{
			printf("Creating thread #%d failed.\n", i);
			n_thr = i;
			break;
		}
		printf("Created thread %lx.\n", (long)st[i].id);
	}

	for (n_running = n_total = n_thr; n_running > 0;)
	{
#if USE_SPROC || USE_THR
		pthread_t id;
#endif

		/* Wait for subthreads to finish. */
#if USE_PTHREADS
		pthread_cond_wait(&finish_cond, &finish_mutex);
		for (i = 0; i < n_thr; i++)
		{
			if (st[i].flags)
			{
				pthread_join(st[i].id, NULL);
				st[i].flags = 0;
				my_end_thread(&st[i]);
			}
		}
#elif USE_THR
		thr_join(0, &id, NULL);
		for (i = 0; i < n_thr; i++)
		{
			if (id == st[i].id)
			{
				my_end_thread(&st[i]);
				break;
			}
		}
#elif USE_SPROC
		{
			int status = 0;
			id = wait(&status);
			if (status)
			{
				if (WIFSIGNALED(status))
				{
					printf("thread %id terminated by signal %d\n",
						   id, WTERMSIG(status));
				}
				else
				{
					printf("thread %id exited with status %d\n",
						   id, WEXITSTATUS(status));
				}
			}

			for (i = 0; i < n_thr; i++)
			{
				if (id == st[i].id)
				{
					my_end_thread(&st[i]);
					break;
				}
			}
		}
#else /* NO_THREADS */
		for (i = 0; i < n_thr; i++) my_end_thread(&st[i]);
		break;
#endif
	}
#if USE_PTHREADS
	pthread_mutex_unlock(&finish_mutex);
#endif

	for (i = 0; i < n_thr; i++)
	{
		if (st[i].sp) free(st[i].sp);
	}
	free(st);
//#if USE_MALLOC
	malloc_stats();
//#endif
	printf("Done.\n");
	return 0;
}
