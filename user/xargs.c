#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

void run_cmd(char* line, char* base_argv[]);

int main(int argc, char* argv[]){
    if(argc < 2){
        fprintf(2, "Usage: xargs [cmd] [args...]\n");
        exit(1);
    }

    char line[512];
    int len = 0;
    char ch;
    // when "find . b | xargs grep hello"
    // it means command output(1) connect to the input(0) by pipe(|)
    // so we just read standard input(0) that we can read "./b\n ./a/b\0"
    while(read(0, &ch, 1) == 1){ 
        if(ch == '\n'){
            line[len] = 0;
            run_cmd(line, argv); // argv: 0[xargs] 1[command] 2[args]
            len = 0;
        }else if(len < sizeof(line) - 1){
            line[len++] = ch;
        }
    }
    if(len){
        line[len] = 0;
        run_cmd(line, argv);
    }
    exit(0);
}

void run_cmd(char* line, char *base_argv[]){
    char* new_argv[MAXARG];
    int i = 0;
    for(i = 1; base_argv[i]; i++){
        new_argv[i-1] = base_argv[i];
    }
    new_argv[i-1] = line;
    new_argv[i] = 0;
    int pid = fork();
    if(pid == 0){
        exec(new_argv[0], new_argv);
        fprintf(2, "xargs: exec failed...\n");
        exit(1);
    }
    wait(0);
}
