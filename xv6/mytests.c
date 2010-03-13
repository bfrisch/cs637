#include "types.h"
#include "user.h"
#include "printf.h"
#include "thread.h"

#define ITERATIONS 100000
#define COND_ITERS 5
#define BUFF_SIZE 10
#define NUM_PROCS 3
#define NUM_THREADS 9

volatile int x = 0;
volatile int numfilled = 0;
volatile int cond_arr[BUFF_SIZE];
mutex_t x_lock;
mutex_t print_lock;

cond_t empty, fill;
mutex_t cond_mutex;

void ticks_test() {
  printf(1, "Current tick count is now: %d!\n", get_cpu_ticks());
  sleep(100);
  printf(1, "Current tick count is now: %d!\n", get_cpu_ticks());
}

void process_test() {
  int i;
  for (i = 1; i <= NUM_PROCS; i++) {
        int currentrunT = 0;
        int myprocid = fork();
        if (myprocid == 0) {
          // spinloop

          // Set proportional ticket count             
	  proc_tickets(i*100);

	  for(currentrunT = 0; currentrunT < 100000; currentrunT++) {
	    if (get_cpu_ticks() % 50 == 0) { 
	      printf(1,"Time: %d, TicketNum %d, RunCnt: %d, PID:%d\n", get_cpu_ticks(), proc_tickets(0), currentrunT, getpid());
	    }
          }
          exit();
        } else if (myprocid < 0) {
	  printf(1, "Error launcing process %d!\n", i);
	  exit();
	}
  }
   
  while(i > 1) {
    int processId = wait();
    printf(1, "Process %d finished!\n", processId);
    i--;
  }
}

void thread_test(void *(*start_routine)(void*)) {
  int i;
  for (i = 0; i < NUM_THREADS; i++) { 
    int mythreadid = thread_create(start_routine, &x);
    mutex_lock(&print_lock);    
    printf(1, "Create: %d!\n", mythreadid);
    mutex_unlock(&print_lock);
  }
  
  while (i > 0) {
    int threadId = thread_wait();
    mutex_lock(&print_lock);
    printf(1, "Finished thread %d at %d!\n", threadId, x);
    mutex_unlock(&print_lock);
    i--;
  }
    
  printf(1, "Expected x: %d  Total for x: %d\n", NUM_THREADS * ITERATIONS, x);
}

void
race_increment(void* arg) 
{
  int i;
  printf(1, "Begin: %d!\n", getpid());
  for(i = 0; i < ITERATIONS; i++) {
    //printf(1, "%d t%d!\n", x, getpid());
    int xTemp = x;
    xTemp++;
    x=xTemp;
  }
}

void
lock_increment(void* arg) 
{
  int i;
  mutex_lock(&print_lock);
  printf(1, "Lock Begin: %d!\n", getpid());
  mutex_unlock(&print_lock);
  for(i = 0; i < ITERATIONS; i++) {
    mutex_lock(&x_lock);
    int xTemp = x;
    xTemp++;
    x=xTemp;
    mutex_unlock(&x_lock);
  }
}

void *producer(void *arg) {
  int i, j;
  for (i = 0; i < COND_ITERS; i++) {
    mutex_lock(&cond_mutex);            // p1
    while (numfilled == BUFF_SIZE)      // p2
      cond_wait(&empty, &cond_mutex);   // p3
    for (j = 0; j < BUFF_SIZE; j++) {
      cond_arr[j] = i;                  // p4
      numfilled++;
    }
    cond_signal(&fill);                 // p5
    mutex_unlock(&cond_mutex);          // p6
  }
  return 0;
}

void *consumer(void *arg) {
  int i, j;
  for (i = 0; i < COND_ITERS; i++) {
    mutex_lock(&cond_mutex);
    while (numfilled == 0)
      cond_wait(&fill, &cond_mutex);
    for (j = 0; j < BUFF_SIZE; j++) {
      printf(1, "%d ", cond_arr[j]);
      numfilled--;
    }
    printf(1, "\n");
    cond_signal(&empty);
    mutex_unlock(&cond_mutex);
  }
  return 0;
}

void cond_test() {
  if (thread_create(&producer, 0) < 1 || thread_create(&consumer, 0) < 1 || thread_create(&consumer, 0) < 1) {
    printf(1, "ERROR!!!! create_thread failed somewhere!!");
    return;
  }
  thread_wait();
  thread_wait();
  thread_wait();
}

void cache_test() {
/*  int fd = open("cacheTest", O_CREATE|O_RDWR);
  char* string = "I am a long string of text that should go in the file so that it will not be all cached.  At least I hope that is the case, as if it is not, I will be sad and very unhappy in generall!!!!  That is all for now in cs 637.  which is a class of fun and overall excitement.  Thanks, and have a good day!";
  if (write(fd, string, strlen(string) < 0)) {
      printf(1, "Error!! Write failed!\n");
  }
  close(fd);

  int fd = open("cacheTest", O_RDONLY);
  //open("test"
  //printf(*/
}

int main(int argc, char* argv[]) 
{
  int testNum = 2;
  if (argc == 2) {
    testNum = atoi(argv[1]);
  }

  switch (testNum) {
  case 1:
    ticks_test();
    break;
  case 2:
    process_test();
    break;
  case 3:
    thread_test(&race_increment);
    break;
  case 4:
    thread_test(&lock_increment);
    break;
  case 5:
    cond_test();
    break;
  case 6:
    cache_test();
    break;
  default:
    printf(1, "Invalid Argument.\nUsage: mytests <test_num>\n");
  }
  exit();
}
