#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
// 该程序用于输出素数，使用了管道和进程来实现(并发筛选)
// 进程0负责生成2~35的整数，并将它们写入管道
// 进程1负责从管道中读取第一个数（素数），输出它，并过滤掉它的倍数，将剩下的数写入另一个管道
// 每个进程只需要知道一个素数（从管道读到的第一个数），然后过滤它的倍数，把剩下的传给下一个进程。
 
//不用的管道端口关闭，避免不到35文件描述符被占满导致无法创建新管道
void primes(int pleft[2])
{
    //从左邻居(父进程)读取整数
    int p;
    read(pleft[0], &p, sizeof(p));
    if (p == -1)
    {
        exit(0);// 如果读取到-1，表示结束，退出进程
    }
    printf("prime %d\n", p);

    int pright[2];
    pipe(pright);// 创建一个新的管道，用于传递过滤后的整数给右邻居(子进程)

    if (fork() == 0)
    {
        close(pright[1]);//子进程用不到写端，关闭它
        close(pleft[0]);// 子进程用不到读端，关闭它
        primes(pright);// 递归调用primes函数，继续过滤下一个素数
    }
    else
    {
        close(pright[0]);//当前进程用不到读端，关闭它
        
        int buf;
        while (read(pleft[0], &buf, sizeof(buf))  && buf != -1)
        {
            if (buf % p != 0)
            {
                write(pright[1], &buf, sizeof(buf));// 将不是p倍数的数写入右邻居的管道
            }
        }  
        //写入-1表示结束，通知右邻居(子进程)退出
        buf = -1;
        write(pright[1], &buf, sizeof(buf));
        wait(0);//等待子进程结束
        exit(0);
    }   
}

int main(int argc, char *argv[])
{
    int input_pipe[2];
    pipe(input_pipe);// 创建一个管道，用于父进程向第一个子进程传递整数

    if(fork() == 0)
    {
        close(input_pipe[1]);//子进程用不到写端，关闭它
        primes(input_pipe);// 调用primes函数，开始筛选素数
        exit(0);
    }
    else
    {
        close(input_pipe[0]);//父进程用不到读端，关闭它
        
        for (int i = 2; i <= 35; i++)
        {
            write(input_pipe[1], &i, sizeof(i));// 将2~35的整数写入管道
        }
        int end_signal = -1;
        write(input_pipe[1], &end_signal, sizeof(end_signal));// 写入-1表示结束，通知子进程退出
        wait(0);//等待子进程结束
        exit(0);
    }
}