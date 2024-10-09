#include "kernel/types.h"
#include "user.h"

#define MAXSIZE 100

int main(int argc, char* argv[]){
    int p1_F2C[2], p2_C2F[2];
    pipe(p1_F2C);
    pipe(p2_C2F);

    int father_pid = getpid();

    int pid = fork();
    if (pid == 0) { 
        //子进程 
        char child_buffer[10];
        close(p1_F2C[1]); 
        read(p1_F2C[0], child_buffer, MAXSIZE);
        printf("%d: received %s from pid %d\n", getpid(), child_buffer, father_pid);        
        close(p1_F2C[0]); 
        close(p2_C2F[0]); 
        write(p2_C2F[1], "pong", 10);       
        close(p2_C2F[1]); 
    } else { 
        //父进程 
        char father_buffer[10];
        close(p1_F2C[0]); 
        write(p1_F2C[1], "ping", 10);      
        close(p1_F2C[1]); 
        close(p2_C2F[1]); 
        read(p2_C2F[0], father_buffer, MAXSIZE);
        printf("%d: received %s from pid %d\n", getpid(), father_buffer, pid);        
        close(p2_C2F[0]); 
    }

    exit(0);
}
