#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#define BUFSIZE 512

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
find(char *path, char* target)
{
    // printf("path: %s\n", path);
  char *base_name;
  char *p;
  int fd;
  struct dirent de;
  struct stat st;
  // Find first character after last slash.
  for(base_name=path+strlen(path); base_name >= path && *base_name != '/'; base_name--);
  base_name++;


  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
    if (strcmp(base_name, target) == 0)
        printf("%s\n", path);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > BUFSIZE){
      printf("ls: path too long\n");
      break;
    }
    p = path+strlen(path); // 移动到当前末端
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){ // 目录存在文件里，逐个读出目录结构体
      if(de.inum == 0 || memcmp("..", de.name, 2) == 0 || memcmp(".", de.name, 1) == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(path, &st) < 0){
        printf("ls: cannot stat %s\n", path);
        continue;
      }
      find(path, target);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;
  char buf[BUFSIZE], *p;
//   printf("argc %d\n", argc);
  if(argc < 2){
    fprintf(2, "Usage: find [path...] [expression]\n");
    exit(1);
  }
  if (argc == 2) {
    strcpy(buf, ".");
    find(buf, argv[1]);
  } else {
    for(i=1; i<argc-1; i++) {
      strcpy(buf, argv[i]);
      p = buf+strlen(buf);
      if (--p >= buf && *p == '/') *p = 0;
      find(buf, argv[argc-1]);
    }
      
  }
  exit(0);
}

/*
echo > b
mkdir a
echo > a/b
mkdir a/aa
echo > a/aa/b
find . b

*/