#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void prime(int* p);

int main(int argc, char* argv[]){

    int p[2];
    pipe(p);
    int pid =  fork();
    if(pid < 0){
        fprintf(2, "fork failed...\n");
        exit(1);
    }

    if(pid == 0){
        prime(p);
    }else{
        close(p[0]);
        for(int i = 2; i <= 35; i++){
            if(write(p[1], &i, sizeof(i)) != sizeof(i)){
                fprintf(2, "pid %d : write failed...", getpid());
                exit(1);
            }
        }
        close(p[1]);
        wait(0);
    }
    exit(0);
}

void prime(int *p){
    close(p[1]);
    int num, next;
    if(read(p[0], &num, sizeof(num)) != sizeof(num)){
        fprintf(2, "pid %d : read failed...\n");
        exit(1);
    }
    printf("prime %d\n", num);

    if(read(p[0], &next, sizeof(next)) == 0){
        exit(0);
    }
    int np[2];
    pipe(np);
    int ppid = fork();
    if(ppid == 0){
        prime(np);
    }else{
        close(np[0]);
        do{
            if(next % num != 0){
                write(np[1], &next, sizeof(next));
            }
        }while(read(p[0], &next, sizeof(next)));
        close(np[1]);
        wait(0);
        exit(0);
    }
    exit(0);
}