#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define BUFFER_SIZE 4

int main(int argc, char* argv[]){
    if(argc > 1){
        fprintf(2, "Usage: pingpong\n");
        exit(1);
    }

    int p2c[2], c2p[2];
    pipe(p2c);
    pipe(c2p);
    int pid = fork();
    char buf[BUFFER_SIZE];

    if(pid == 0){
        close(p2c[1]);
        read(p2c[0], buf, BUFFER_SIZE);
        close(p2c[0]);
        int cpid = getpid();
        fprintf(1, "%d: received ping\n", cpid);
        close(c2p[0]);
        write(c2p[1], "pong", 4);
        close(c2p[1]);
        exit(0);

    }else{
        close(p2c[0]);
        write(p2c[1], "ping", 4);
        close(p2c[1]);
        wait(0);
        read(c2p[0], buf, BUFFER_SIZE);
        close(c2p[0]);
        int ppid = getpid();
        fprintf(1, "%d: received pong\n", ppid);
        exit(0);
    }

    exit(0);
}