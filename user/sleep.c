#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"


int
main(int argc, char *argv[])
{
  if(argc <= 1){
    fprintf(2, "sleep: error\n");
    exit(1);
  }
  int st = atoi(argv[1]);
  fprintf(1, "(nothing happens for a little while)\n");
  sleep(st);
  exit(0);
}
