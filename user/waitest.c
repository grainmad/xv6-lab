// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define N  1000

void
waitest(void) {
  int pid = fork();
  if (pid == 0) {
    sleep(50);
    exit(123);
  } else {
    int t;
    wait(&t);
    printf("son status: %d\n", t);
  }
}
int
main(void)
{
  waitest();
  exit(0);
}
