#ifndef __THREADS_H_
#define __THREADS_H_

#include "types.h"
#include "threads.h"
#include "threadsInt.h"
#include "user.h"

int thread_create(void *(*start_routine)(void*), void* arg) {
  void* stack;
  if ((stack = malloc(1024)) != 0) {
    memmove(stack + 1016, start_routine, sizeof(start_routine));
    memmove(stack + 1020, arg, sizeof(arg));
  } else {
    return -1;
  }
  int childThreadId = thread_fork(stack);
  if (childThreadId == 0) {
    start_routine(arg);
    exit();
  }
  return childThreadId;
}

void thread_wait(void) {
  int pid;
  while ((pid = thread_wait_internal()) != -1);
  return;
}

#endif /* __THREADS_H_ */
