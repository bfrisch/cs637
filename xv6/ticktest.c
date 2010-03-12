#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf(1, "Current tick count is now: %d!\n", get_cpu_ticks());
  sleep(100);
  printf(1, "Current tick count is now: %d!\n", get_cpu_ticks());
  exit();
}
