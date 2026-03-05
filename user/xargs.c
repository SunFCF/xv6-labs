#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

//从标准输入读取参数，并将参数提供给指定命令执行
void run(char *program, char **args)
{
    if (fork() == 0)
    {   //在子进程中执行新的程序
        exec(program, args);
        fprintf(2, "exec %s failed\n", program);
        exit(1);
    }
    return;
}

int main(int argc, char *argv[])
{
    char buf[2048]; // 读入时使用的内存池
	char *p = buf, *last_p = buf; // 当前参数的结束、开始指针
	char *argsbuf[128]; // 全部参数列表，字符串指针数组，包含 argv 传进来的参数和 stdin 读入的参数
	char **args = argsbuf; // 指向 argsbuf 中第一个从 stdin 读入的参数

    for (int i = 1; i < argc; i++)
    {
        *args++ = argv[i];// 将 argv 中的参数添加到 argsbuf 中
    }
    
    char** args_start = args; // 记录从 stdin 读入的参数的起始位置

    while (read(0, p, 1) > 0)
    {   // 从标准输入逐字节读取，直到 EOF
        char c = *p;
        // 将空格替换为 \0 分割开各个参数，这样可以直接使用内存池中的字符串作为参数字符串
        // 而不用额外开辟空间
        if (c == ' ' || c == '\n')
        {   // 遇到空格或换行，表示一个参数的结束
            *p = '\0'; // 将参数字符串以 '\0' 结尾
            
            *(args_start++) = last_p; // 将参数的起始地址添加到 args 中
            last_p = p + 1; // 更新 last_p 指向下一个参数的起始位置
        }

        if (c == '\n')
        {
            *args_start = 0; // 参数列表以 NULL 结尾，表示参数结束
            run(argv[1], argsbuf); // 执行指定命令，传入参数列表
            args_start = args; // 重置 args_start，准备读取下一行参数
        }
        
        p++; // 移动指针，准备读取下一个字符
    }
    
    // 处理最后一行参数（如果没有以换行结尾）
    if (args_start != args)
    {
        *p = '\0'; // 将最后一个参数以 '\0' 结尾
        *(args_start++) = last_p; // 将最后一个参数的起始地址添加到 args 中
        *args_start = 0; // 参数列表以 NULL 结尾，表示参数结束

        run(argv[1], argsbuf); // 执行指定命令，传入参数列表
    }

    while (wait(0) != -1)
    {
        // 等待所有子进程结束
    }
    
    exit(0);
}