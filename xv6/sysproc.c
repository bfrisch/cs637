#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

struct spinlock ticketslock;

void ticketlockinit(void) {
  initlock(&ticketslock, "tickets");
}

int
sys_fork(void)
{
  int pid;
  struct proc *np;

  if((np = copyproc(cp)) == 0)
    return -1;
  pid = np->pid;
  np->state = RUNNABLE;
  return pid;
}


int
sys_thread_fork(void)
{
  int pid;
  struct proc *np;
  int stack;
  if (argint(0, &stack) < 0) {
    return -1;
  } 
  if((np = copyproc_thread(cp, stack)) <= 0)
    return -1;
  pid = np->pid;
  np->state = RUNNABLE;
  return pid;
}

int sys_proc_tickets(void) {
  int newTickcount;
  if (argint(0, &newTickcount) < 0) {
    return -1;
  }
  if (newTickcount > 0) {
    acquire(&ticketslock);
    ttltcts -= cp->tctcnt;
    cp->tctcnt = newTickcount;
    ttltcts += cp->tctcnt;
    release(&ticketslock);
  } else if (newTickcount < 0) {
    return -1;
  }
  return cp->tctcnt;
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return cp->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  if((addr = growproc(n)) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n, ticks0;
  
  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(cp->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

unsigned int
sys_fcount(void)
{
  int fc;
  if(argint(0, &fc) < 0)
    return -1;
  return fcount();
}

int sys_tickcount_sc() {
  return ticks;
}

int sys_thread_wait() {
  return wait();
}

int sys_cond_sleep_and_unlock_mutex(void) {
  int cond_addr, mutex_addr;
  if (argint(0, &cond_addr) < 0 || argint(1, &mutex_addr) < 0) {
    return -1;
  }
  sleep_on_cond(cond_addr, mutex_addr);
}

int sys_cond_signal(void) {
  int cond_addr;
  if (argint(0, &cond_addr) < 0) {
    return -1;
  }
  return wake_on_cond(cond_addr);
}
