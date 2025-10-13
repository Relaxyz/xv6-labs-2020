#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char* path, char* file);

int main(int argc, char* argv[]){
    if(argc < 3){
        fprintf(2, "Usage: find [dir/file] [file]\n");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}

void find(char* path, char* file){
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        exit(1);
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        exit(1);
    }

    switch(st.type){
        case(T_FILE):
            for(p = path + strlen(path); p >= path && *p != '/'; p--);
            p++;
            if(strcmp(p, file) == 0){
                printf("%s\n", path);
            }
            break;
        case(T_DIR):
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0) continue;
                if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;
                int pl = strlen(path);
                int dl = strlen(de.name);
                if(pl + 1 + dl + 1 > sizeof(buf)){
                    fprintf(2, "find: path is too long...\n");
                    continue;
                }
                memmove(buf, path, pl);
                buf[pl] = '/';
                memmove(buf + pl + 1, de.name, dl);
                buf[pl + 1 + dl] = 0;
                find(buf, file);
            }
            break;
    }
    close(fd);
}