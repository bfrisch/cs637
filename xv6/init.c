// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *sh_args[] = {"sh", 0 };
char *fsck_args[] = { "fsck", 0 };
char *pbjd_args[] = { "pbjd", 0 };

int
main(void)
{
  int pid, wpid;

  printf(1, "init: running fsck\n");
  pid = fork();
  if (pid < 0) {
    printf(1, "init: fork failed\n");
    exit();
  }
  if(pid == 0){
    exec("fsck", fsck_args);
    printf(1, "init: exec fsck failed\n");
    exit();
  }
  wpid=wait();

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  printf(1, "init: starting pbjd\n");
  pid = fork();
  if (pid < 0) {
    printf(1, "init: fork failed\n");
    exit();
  }
  if(pid == 0){
    exec("pbjd", pbjd_args);
    printf(1, "init: exec pbjd failed\n");
    exit();
    }

  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", sh_args);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
