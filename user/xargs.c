#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char buf[512];
char* exec_args[32];

int
main(int argc, char *argv[])
{
  if (argc<2) {
    fprintf(2, "xargs: param error\n");
    exit(1);
  }
    
  // for (int i=0; i<argc; i++) {
  //   printf("arg %d : %s\n", i, argv[i]);
  // }
  int n, p = 0, pid, status;
  for (n=1; n<argc; n++) exec_args[n-1] = argv[n];
  while (1) {
    pid = fork();
    if (pid == 0) {
      while ((n = read(0, buf+p, 1)) > 0 && *(buf+p) != '\n') p++;
      if (p == 0) exit(-1);
      *(buf+p) = 0;
      exec_args[argc-1] = buf;
      // printf("exec %s\n", argv[1]);
      // for (int i=0; i<=argc-1; i++) {
      //   printf("exec arg %d : %s\n", i, exec_args[i]);
      // }
      exec(argv[1], exec_args);
      exit(0);
    } else if (pid<0) {
      fprintf(2, "fork error\n");
    } else {
      wait(&status);
      if (status == -1) break;
    }
  }
  exit(0);
}
