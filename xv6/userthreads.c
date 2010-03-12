#include "types.h"
#include "thread.h"
#include "threadsInt.h"
#include "user.h"

static inline uint
xchg(volatile uint *addr, uint newval)
{
  uint result;
  
  // The + in "+m" denotes a read-modify-write operand.
  asm volatile("lock; xchgl %0, %1" :
               "+m" (*addr), "=a" (result) :
               "1" (newval) :
               "cc");
  return result;
}

int thread_create(void *(*start_routine)(void*), void* arg) {
  void* stack;
  if ((stack = malloc(1024)) == 0) {
    return -1;
  }
  int childThreadId = thread_fork(stack);
  if (childThreadId == 0) {
    (*start_routine)(arg);
    exit();
  }
  return childThreadId;
}

int
cond_wait(cond_t *c, mutex_t *m)
{
  if (cond_sleep_and_unlock_mutex(c, m) != 0) {
    return -1;
  }
  mutex_lock(m);
  return 0;
}

void mutex_lock(mutex_t* m) {
  while(xchg(&(m->locked), 1) == 1)
    ;
}

void mutex_unlock(mutex_t* lock) {
  xchg(&(lock->locked), 0);
}
