//------------------------------------------------------------------------------
//
// memtrace
//
// trace calls to the dynamic memory manager
//
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memlog.h>
#include <memlist.h>

//
// function pointers to stdlib's memory management functions
//
static void *(*mallocp)(size_t size) = NULL;
static void (*freep)(void *ptr) = NULL;
static void *(*callocp)(size_t nmemb, size_t size);
static void *(*reallocp)(void *ptr, size_t size);

//
// statistics & other global variables
//
static unsigned long n_malloc  = 0;
static unsigned long n_calloc  = 0;
static unsigned long n_realloc = 0;
static unsigned long n_allocb  = 0;
static unsigned long n_freeb   = 0;
static item *list = NULL;

//
// init - this function is called once when the shared library is loaded
//
__attribute__((constructor))
void init(void)
{
  char *error;

  LOG_START();

  // initialize a new list to keep track of all memory (de-)allocations
  // (not needed for part 1)
  list = new_list();

  // ...
}

//
// fini - this function is called once when the shared library is unloaded
//
__attribute__((destructor))
void fini(void)
{
  LOG_STATISTICS(n_allocb, n_allocb / (n_malloc + n_calloc + n_realloc), n_freeb);

  int log_nonfreed_started = 0;
  while (list) {
    if (list->cnt > 0) {
      if (log_nonfreed_started == 0) {
        log_nonfreed_started = 1;
        LOG_NONFREED_START();
      }
      LOG_BLOCK(list->ptr, list->size, list->cnt);
    }
    list = list->next;
  }

  LOG_STOP();

  // free list (not needed for part 1)
  free_list(list);
}

void *malloc(size_t size)
{
  void *ptr;
  char *error;

  if (!mallocp) {
    mallocp = dlsym(RTLD_NEXT, "malloc");
    if ((error = dlerror()) != NULL || (mallocp == NULL)) {
      fprintf(stderr, "Error getting symbol 'malloc': %s\n", error);
      exit(1);
    }
  }

  ptr = mallocp(size);
  n_allocb += size;
  n_malloc += 1;
  LOG_MALLOC(size, ptr);

  alloc(list, ptr, size);

  return ptr;
}

void *calloc(size_t nmemb, size_t size)
{
  void *ptr;
  char *error;

  if (!callocp) {
    callocp = dlsym(RTLD_NEXT, "calloc");
    
    if ((error = dlerror()) != NULL || (callocp == NULL)) {
      fprintf(stderr, "Error getting symbol 'calloc': %s\n", error);
      exit(1);
    }
  }

  ptr = callocp(nmemb, size);
  n_allocb += nmemb * size;
  n_calloc += 1;

  LOG_CALLOC(nmemb, size, ptr);

  alloc(list, ptr, nmemb * size);

  return ptr;
}

void *realloc(void *ptr, size_t size)
{  
  char *error;
  void *realloced_ptr;

  if (!reallocp) {      
    reallocp = dlsym(RTLD_NEXT, "realloc");
    if ((error = dlerror()) != NULL || (reallocp == NULL)) {
      fprintf(stderr, "Error getting symbol 'realloc': %s\n", error);
      exit(1);
    }
  }

  realloced_ptr = reallocp(ptr, size);
  n_allocb += size;
  n_realloc += 1;
  n_freeb += dealloc(list, ptr)->size;
  alloc(list, realloced_ptr, size); 

  LOG_REALLOC(ptr, size, realloced_ptr); 

  return realloced_ptr;   
}

void free(void *ptr)
{
  char *error;

  if(!freep) {
    freep = dlsym(RTLD_NEXT, "free");
    if ((error = dlerror()) != NULL || (freep == NULL)) {
      fprintf(stderr, "Error getting symbol 'free': %s\n", error);
      exit(1);
    }
  }

  n_freeb += dealloc(list, ptr)->size;

  freep(ptr);
    
  LOG_FREE(ptr);  
}