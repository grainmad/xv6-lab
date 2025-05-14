#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define BUFSZ  ((12)*1024)

char buf[BUFSZ];

void
siglewrite(char *s)
{
  int howmany = 20;
  char name[3];
  name[0] = 'a';
  name[1] = 'a';
  name[2] = '\0';
  for (int i=0; i<howmany; i++) 
  for (int j=0; j<howmany; j++) {
    name[0] = 'a'+i;
    name[1] = 'a'+j;
    unlink(name);
    int fd = open(name, O_CREATE | O_RDWR);
    if(fd < 0){
      printf("%s: cannot create %s\n", s, name);
      return ;
    }
    int sz = sizeof(buf);
    int cc = write(fd, buf, sz);
    if(cc != sz){
      printf("%s: write(%d) ret %d\n", s, sz, cc);
      return ;
    }
    printf(".");
    close(fd);
  }
  printf("\n");
  for (int i=0; i<howmany; i++) 
  for (int j=0; j<howmany; j++) {
    name[0] = 'a'+i;
    name[1] = 'a'+j;
    unlink(name);
    printf(".");
  }
  printf("\n");
}

void
manywrites(char *s)
{
  int nchildren = 5;
  int howmany = 60; // increase to look for deadlock
  
  for(int ci = 0; ci < nchildren; ci++){
    int pid = fork();
    if(pid < 0){
      printf("fork failed\n");
      exit(1);
    }

    if(pid == 0){
      char name[3];
      name[0] = 'b';
      name[1] = 'a' + ci;
      name[2] = '\0';
      unlink(name);
      
      for(int iters = 0; iters < howmany; iters++){
        for(int i = 0; i < ci+1; i++){
          int fd = open(name, O_CREATE | O_RDWR);
          if(fd < 0){
            printf("%s: cannot create %s\n", s, name);
            exit(1);
          }
          int sz = sizeof(buf);
          int cc = write(fd, buf, sz);
          if(cc != sz){
            printf("%s: write(%d) ret %d\n", s, sz, cc);
            exit(1);
          }
          close(fd);
        }
        unlink(name);
      }

      unlink(name);
      exit(0);
    }
  }

  for(int ci = 0; ci < nchildren; ci++){
    int st = 0;
    wait(&st);
    if(st != 0)
      exit(st);
  }
}

int
main(int argc, char *argv[])
{
  uint start = uptime();
  manywrites("blocktest");
  uint end = uptime();
  printf("testblock: %d.%d s\n", (end - start)/10, (end - start)%10);
  exit(0);
}
