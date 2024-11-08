#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int fds[2];
int fds2[2];

int prime[281];
int cur = 1;

void sol() {
    int p;
    read(fds[0], &p, sizeof(int));
    if (prime[++cur] == 0) {
        printf("prime %d\n", p);
        write(fds2[1], &cur, sizeof(int));
        close(fds[0]);
        close(fds[1]);
        close(fds2[0]);
        close(fds2[1]);
        exit(0);
    }
    if (p%prime[cur]) {
        // printf("pid=%d: write %d\n", getpid(), p);
        write(fds[1], &p, sizeof(int));
        if (fork() == 0) {
            sol();
        }
        else {
            close(fds[0]);
            close(fds[1]);
            close(fds2[0]);
            close(fds2[1]);
            wait(0);
        }
    } else {
        p = 0;
        write(fds2[1], &p, sizeof(int));
        close(fds[0]);
        close(fds[1]);
        close(fds2[0]);
        close(fds2[1]);
    }
    exit(0);
}

int
main(int argc, char *argv[])
{
    pipe(fds);
    pipe(fds2);
    int rt = 0;
    for (int i=2; i<=280; i++) {
        // printf("pid=%d: write %d\n", getpid(), i);
        write(fds[1], &i, sizeof(int));
        if (fork() == 0) {
            sol();
        }
        else {
            wait(0);
            read(fds2[0], &rt, sizeof(int));
            if (rt != 0) prime[rt] = i;
        }
    }
    close(fds[0]);
    close(fds[1]);
    close(fds2[0]);
    close(fds2[1]);
    exit(0);
    
}
