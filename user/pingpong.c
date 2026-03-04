#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int fd_p2c[2], fd_c2p[2];

    pipe(fd_p2c);
    pipe(fd_c2p);

    int pid = fork();

    if (pid < 0)
    {   
        // 创建管道失败或创建子进程失败，关闭所有打开的文件描述符并退出
        close(fd_p2c[0]);
        close(fd_p2c[1]);
        close(fd_c2p[0]); 
        close(fd_c2p[1]);
        fprintf(2, "fork failed\n");
        exit(1);
    }
    else if (pid == 0)
    {
        //子进程：负责接收父进程的消息并回复
        char buf;
        read(fd_p2c[0], &buf, 1);
        printf("%d: received ping\n", getpid());

        write(fd_c2p[1], &buf, 1);
        //写端不用就关闭，不然容易读取时会阻塞
        close(fd_c2p[1]);
    }
    else
    {
        //父进程：负责发送消息给子进程
        write(fd_p2c[1], "p", 1);//发送一个字节的消息，消息为字符'p'
        close(fd_p2c[1]);

        char buf;
        read(fd_c2p[0], &buf, 1);
        printf("%d: received pong\n", getpid());
        
        wait(0);//等待子进程结束
    }
    
    close(fd_p2c[0]);
    close(fd_c2p[0]);
    exit(0);
}