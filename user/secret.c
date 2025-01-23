#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"


int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf("Usage: secret the-secret\n");
    exit(1);
  }
  printf("[attack debug] secret\n");
  char *end = sbrk(PGSIZE*32);
  printf("[attack debug]  end ptr: %p\n", (void *) end);
  end = end + 9 * PGSIZE;
  printf("[attack debug] end ptr: %p\n", (void *) end);
  strcpy(end, "my very very very secret pw is:   ");
  strcpy(end+32, argv[1]);
  printf("[attack debug] end+32 string: %s\n", end);
  exit(0);
}

