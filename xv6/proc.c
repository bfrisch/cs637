#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "pbj.h"

struct spinlock proc_table_lock;

struct proc proc[NPROC];
static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void forkret1(struct trapframe*);
extern unsigned int fastrand(unsigned int s);
int ttltcts = 0; 

struct mem_trans*
curmt(void) {
  return curproc()->ct;
}

void
resetcurtrans(void) {
  struct mem_trans* ct = (struct mem_trans*) kmalloc(sizeof(struct mem_trans));
  ct->size = 0;
  ct->bufs = 0;
  curproc()->ct = ct;
}

void
pinit(void)
{
  initlock(&proc_table_lock, "proc_table");
}

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and return it.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  int i;
  struct proc *p;

  acquire(&proc_table_lock);
  for(i = 0; i < NPROC; i++){
    p = &proc[i];
    if(p->state == UNUSED){
      p->state = EMBRYO;
      p->pid = nextpid++;
      p->tctcnt = 100;

      p->ct = (struct mem_trans*) kmalloc(sizeof(struct mem_trans));
      p->ct->bufs = 0;
      p->ct->size = 0;

      ttltcts += 100;
      release(&proc_table_lock);
      return p;
    }
  }
  release(&proc_table_lock);
  return 0;
}

// Grow current process's memory by n bytes.
// Return old size on success, -1 on failure.
int
growproc(int n)
{
  char *newmem;

  newmem = kalloc(cp->sz + n);
  if(newmem == 0)
    return -1;
  memmove(newmem, cp->mem, cp->sz);
  memset(newmem + cp->sz, 0, n);
  kfree(cp->mem, cp->sz);
  cp->mem = newmem;
  cp->sz += n;
  setupsegs(cp);
  return cp->sz - n;
}

// Set up CPU's segment descriptors and task state for a given process.
// If p==0, set up for "idle" state for when scheduler() is running.
void
setupsegs(struct proc *p)
{
  struct cpu *c;
  
  pushcli();
  c = &cpus[cpu()];
  c->ts.ss0 = SEG_KDATA << 3;
  if(p)
    c->ts.esp0 = (uint)(p->kstack + KSTACKSIZE);
  else
    c->ts.esp0 = 0xffffffff;

  c->gdt[0] = SEG_NULL;
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0x100000 + 64*1024-1, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_TSS] = SEG16(STS_T32A, (uint)&c->ts, sizeof(c->ts)-1, 0);
  c->gdt[SEG_TSS].s = 0;
  if(p){
    c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, (uint)p->mem, p->sz-1, DPL_USER);
    c->gdt[SEG_UDATA] = SEG(STA_W, (uint)p->mem, p->sz-1, DPL_USER);
  } else {
    c->gdt[SEG_UCODE] = SEG_NULL;
    c->gdt[SEG_UDATA] = SEG_NULL;
  }

  lgdt(c->gdt, sizeof(c->gdt));
  ltr(SEG_TSS << 3);
  popcli();
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
struct proc*
copyproc(struct proc *p)
{
  int i;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return 0;

  // Allocate kernel stack.
  if((np->kstack = kalloc(KSTACKSIZE)) == 0){
    np->state = UNUSED;
    return 0;
  }
  np->tf = (struct trapframe*)(np->kstack + KSTACKSIZE) - 1;

  if(p){  // Copy process state from p.
    np->parent = p;
    memmove(np->tf, p->tf, sizeof(*np->tf));
  
    np->sz = p->sz;
    if((np->mem = kalloc(np->sz)) == 0){
      kfree(np->kstack, KSTACKSIZE);
      np->kstack = 0;
      np->state = UNUSED;
      np->parent = 0;
      return 0;
    }
    memmove(np->mem, p->mem, np->sz);

    for(i = 0; i < NOFILE; i++)
      if(p->ofile[i])
        np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);
  }

  // Set up new context to start executing at forkret (see below).
  memset(&np->context, 0, sizeof(np->context));
  np->context.eip = (uint)forkret;
  np->context.esp = (uint)np->tf;

  // Clear %eax so that fork system call returns 0 in child.
  np->tf->eax = 0;
  return np;
}

// Create a new thread copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
struct proc*
copyproc_thread(struct proc *p, int usrstck)
{
  int i;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return 0;

  // Allocate kernel stack.
  if((np->kstack = kalloc(KSTACKSIZE)) == 0){
    np->state = UNUSED;
    return 0;
  }
  np->tf = (struct trapframe*)(np->kstack + KSTACKSIZE) - 1;

  if(p){  // Copy process state from p.
    np->parent = p;
    memmove(np->tf, p->tf, sizeof(*np->tf));
  
    np->sz = p->sz;
    np->mem = p->mem;

    for(i = 0; i < NOFILE; i++) 
      if(p->ofile[i])
        np->ofile[i] = p->ofile[i];
    np->cwd = p->cwd;
  }

  // Set up new context to start executing at forkret (see below).
  memset(&np->context, 0, sizeof(np->context));
  np->context.eip = (uint)forkret;
  np->context.esp = (uint)np->tf;

  // set new thread to point to new stack.
  np->tf->ebp = (uint)(usrstck) + 1008;
  np->tf->esp = (uint)(usrstck) + 980;
  memmove(np->mem + (uint)(np->tf->esp), (p->mem + p->tf->esp), 44);
  
  // Clear %eax so that fork system call returns 0 in child.
  np->tf->eax = 0;
  return np;
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern uchar _binary_initcode_start[], _binary_initcode_size[];
  
  p = copyproc(0);
  p->sz = PAGE;
  p->mem = kalloc(p->sz);
  p->cwd = namei("/");
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = p->sz;
  
  // Make return address readable; needed for some gcc.
  p->tf->esp -= 4;
  *(uint*)(p->mem + p->tf->esp) = 0xefefefef;

  // On entry to user space, start executing at beginning of initcode.S.
  p->tf->eip = 0;
  memmove(p->mem, _binary_initcode_start, (int)_binary_initcode_size);
  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->state = RUNNABLE;
  
  initproc = p;
}

// Return currently running process.
struct proc*
curproc(void)
{
  struct proc *p;

  pushcli();
  p = cpus[cpu()].curproc;
  popcli();
  return p;
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
lottery_scheduler(void)
{
  struct proc *p;
  struct cpu *c;
  int i;
  unsigned int random;
  unsigned int curtct;
  unsigned int nxttct;

  c = &cpus[cpu()];
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&proc_table_lock);

    // random = 0 to ttltcts simulating whose turn it is.
    random = fastrand(ticks) % ttltcts;
    curtct = 0;
    // nxttct = 0;

    // iterate over all processes
    for(i = 0; i < NPROC; i++){
      p = &proc[i];
      if(p->state == UNUSED || p->state == ZOMBIE) { 
          continue;
      }
      // set up proc range.
      nxttct = curtct + p->tctcnt;
      // not in our proc's range, check next range 
      if (!(random >= curtct && random < nxttct)) {
          curtct = nxttct;
          continue;
      }

      // desired proc found, don't loop through anymore.
      i = NPROC; // start looping all processes from begining, but finish this loop.

      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release proc_table_lock and then reacquire it
      // before jumping back to us.
      c->curproc = p;
      setupsegs(p);
      p->state = RUNNING;
      swtch(&c->context, &p->context);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->curproc = 0;
      setupsegs(0);
    }
    release(&proc_table_lock);

  }
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
rr_scheduler(void)
{
  struct proc *p;
  struct cpu *c;
  int i;

  c = &cpus[cpu()];
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&proc_table_lock);
    for(i = 0; i < NPROC; i++){
      p = &proc[i];
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release proc_table_lock and then reacquire it
      // before jumping back to us.
      c->curproc = p;
      setupsegs(p);
      p->state = RUNNING;
      swtch(&c->context, &p->context);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->curproc = 0;
      setupsegs(0);
    }
    release(&proc_table_lock);

  }
}

// Enter scheduler.  Must already hold proc_table_lock
// and have changed curproc[cpu()]->state.
void
sched(void)
{
  if(read_eflags()&FL_IF)
    panic("sched interruptible");
  if(cp->state == RUNNING)
    panic("sched running");
  if(!holding(&proc_table_lock))
    panic("sched proc_table_lock");
  if(cpus[cpu()].ncli != 1)
    panic("sched locks");

  swtch(&cp->context, &cpus[cpu()].context);
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&proc_table_lock);
  cp->state = RUNNABLE;
  sched();
  release(&proc_table_lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  // Still holding proc_table_lock from scheduler.
  release(&proc_table_lock);

  // Jump into assembly, never to return.
  forkret1(cp->tf);
}

// Atomically release lock and sleep on chan.
// Reacquires lock when reawakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(cp == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire proc_table_lock in order to
  // change p->state and then call sched.
  // Once we hold proc_table_lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with proc_table_lock locked),
  // so it's okay to release lk.
  if(lk != &proc_table_lock){
    acquire(&proc_table_lock);
    release(lk);
  }

  // Go to sleep.
  cp->chan = chan;
  cp->state = SLEEPING;
  sched();

  // Tidy up.
  cp->chan = 0;

  // Reacquire original lock.
  if(lk != &proc_table_lock){
    release(&proc_table_lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
// Proc_table_lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&proc_table_lock);
  wakeup1(chan);
  release(&proc_table_lock);
}

// Kill the process with the given pid.
// Process won't actually exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&proc_table_lock);
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&proc_table_lock);
      return 0;
    }
  }
  release(&proc_table_lock);
  return -1;
}

// Exit the current process.  Does not return.
// Exited processes remain in the zombie state
// until their parent calls wait() to find out they exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  int isThread = (cp->parent->mem == cp->mem);

  ttltcts -= cp->tctcnt;

  if(cp == initproc)
    panic("init exiting");


  if (!isThread) {
    // Close all open files.
    for(fd = 0; fd < NOFILE; fd++){
      if(cp->ofile[fd]){
	fileclose(cp->ofile[fd]);
	cp->ofile[fd] = 0;
      }
    }

    iput(cp->cwd);
    cp->cwd = 0;
  }

  acquire(&proc_table_lock);

  // Parent might be sleeping in wait().
  wakeup1(cp->parent);

  // Pass abandoned children to init.
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->parent == cp){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  cp->killed = 0;
  cp->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int i, havekids, pid;

  acquire(&proc_table_lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(i = 0; i < NPROC; i++){
      p = &proc[i];
      if(p->state == UNUSED)
        continue;
      if(p->parent == cp){
        if(p->state == ZOMBIE){
          // Found one.
          if (cp->mem != p->mem) {
	    // Clear the memory only if it isn't your child thread.
	    kfree(p->mem, p->sz);
          }
          kfree(p->kstack, KSTACKSIZE);
          pid = p->pid;
          p->state = UNUSED;
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          release(&proc_table_lock);
          return pid;
        }
        havekids = 1;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || cp->killed){
      release(&proc_table_lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(cp, &proc_table_lock);
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i, j;
  struct proc *p;
  char *state;
  uint pc[10];
  
  for(i = 0; i < NPROC; i++){
    p = &proc[i];
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context.ebp+2, pc);
      for(j=0; j<10 && pc[j] != 0; j++)
        cprintf(" %p", pc[j]);
    }
    cprintf("\n");
  }
}

unsigned int
fcount()
{
  int fd;
  int fc = 0;

  // Loop through all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(cp->ofile[fd]){
      fc++;  // Increment our file counter.
    }
  }

  return fc;
}

int wake_on_cond(int cond_addr) {
  struct proc *p;
  acquire(&proc_table_lock);

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->mem == cp->mem && p->cond_addr == cond_addr) {
      p->state = RUNNABLE;
    }
  }
  release(&proc_table_lock);
  return 0;
}

int sleep_on_cond(int cond_addr, int mutex_lock)
{
  // Sleep
  acquire(&proc_table_lock);
  cp->cond_addr = cond_addr;
  cp->state = SLEEPING;

  // Unlock
  *((int *) (cp->mem + mutex_lock)) = 0;
  
  // Run another process
  sched();
  release(&proc_table_lock);
  return 0;
}
