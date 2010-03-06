#include "types.h"
#include "user.h"
#include "printf.h"
#include "thread.h"

#define ITERATIONS 1000
#define NUM_PROCS 3
#define NUM_THREADS 3

void* increment(void*); 
volatile int x = 0;

void processTest() {
  int i;
  for (i = 1; i <= NUM_PROCS; i++) {
        int currentrunT = 0;
        int myprocid = fork();
        if (myprocid == 0) {
            // spinloop
            
            // Set proportional ticket count 
            proc_tickets(i*100);

            for(currentrunT = 0; currentrunT < 100000; currentrunT++) {
              if (tickcount() % 50 == 0) { 
		printf(1,"Time: %d, TicketNum %d, RunCnt: %d, PID:%d\n", tickcount(), proc_tickets(0), currentrunT, getpid());
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

void threadTest() {
  int i;
  for (i = 0; i < NUM_THREADS; i++) { 
    int mythreadid = thread_create(&increment, (void*) &x);
        
    printf(1, "Starting Thread: %d\n", mythreadid);
  }
  
  while (i > 0) {
    thread_wait();
    printf(1, "Finished thread %d!\n", NUM_THREADS - i + 1);
    i--;
  }
    
  printf(1, "Expected x: %d  Total for x: %d\n", NUM_THREADS * ITERATIONS, x);
}

int main(int argc, char* argv[]) 
{
  int testNum = 1;
  if (argc == 2) {
    testNum = atoi(argv[1]);
  }

  switch (testNum) {
  case 1:
    processTest();
    break;
  case 2:
    threadTest();
    break;
  default:
    printf(1, "Invalid Argument.\nUsage: mytests <test_num>\n");
  }
  exit();
}

void*
increment(void* arg) 
{
  int* myX = (int*) arg;
  int i;
  for(i = 0; i < ITERATIONS; i++) {
    (*(myX))++;
  }
  return 0;
}
