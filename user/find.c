#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

//递归查找指定目录下的所有命名为“name”的文件，按名称匹配并输出完整路径

//递归查找函数，查找路径为 path 的目录下是否有目标文件 target
void find(char *path, char *name)
{
    char buf[512], *p;
    int fd;
    struct dirent de;   //目录项结构体，包含文件名和 inode 号
    struct stat st;     //文件状态结构体，包含文件类型(普通文件、目录等)、大小等信息
    //打开目录文件，获取文件描述符
    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    //获取目录文件的状态信息，判断是否为目录
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type)
    {
    case T_FILE:
        //如果是普通文件，判断文件名是否匹配目标文件名
        if (strcmp(path + strlen(path) - strlen(name), name) == 0)
        {
            printf("%s\n", path);//如果匹配成功，输出完整路径
        }
        break;
    
    case T_DIR:
        //如果是目录，递归查找
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        {//如果路径长度超过缓冲区大小(防止溢出)，输出错误信息并返回
            fprintf(2, "find: path too long\n");
            break;
        }
        strcpy(buf, path);//将当前路径复制到缓冲区
        p = buf + strlen(buf);//将指针移动到路径末尾
        *p++ = '/';//在路径末尾添加 '/'，

        //循环读取目录项，直到读完所有目录项
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0)
                continue;//如果目录项的 inode 号为 0，表示该目录项无效，跳过

            memmove(p, de.name, DIRSIZ);//将目录项的文件名复制到路径末尾
            p[DIRSIZ] = 0;//在路径末尾添加字符串结束符

            if (stat(buf, &st) < 0)
            {
                fprintf(2, "find: cannot stat %s\n", buf);
                continue;//如果获取文件状态失败，输出错误信息并跳过
            }

            if (strcmp(buf + strlen(buf) - 3, "/..") == 0 || strcmp(buf + strlen(buf) - 2, "/.") == 0)
            {
                continue;//如果路径以 "/.." 或 "/." 结尾，表示当前目录或父目录，跳过
            }
            
            find(buf, name);//递归调用 find 函数，继续查找子目录
        }
        break;
    }
    close(fd);//关闭目录文件描述符
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(2, "Usage: find <path> <name>\n");
        exit(1);
    }

    char target[512];
    target[0] = '/';//为查找的文件名添加 / 在开头
    strcpy(target + 1, argv[2]);

    find(argv[1], target);
    exit(0);
}