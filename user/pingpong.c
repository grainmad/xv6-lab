#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int pid;
    int fds1[2], fds2[2];;
    pipe(fds1);
    pipe(fds2);
    pid = fork();
    if(pid == 0) {
        char c;
        read(fds1[0], &c, 1);
        printf("%d: received ping\n", getpid());
        write(fds2[1], "a", 1);
        close(fds1[0]);
        close(fds1[1]);
        close(fds2[0]);
        close(fds2[1]);
        exit(0);
    } else {
        char c;
        write(fds1[1], "a", 1);
        read(fds2[0], &c, 1);
        printf("%d: received pong\n", getpid());
        close(fds1[0]);
        close(fds1[1]);
        close(fds2[0]);
        close(fds2[1]);
    }
    exit(0);
}
