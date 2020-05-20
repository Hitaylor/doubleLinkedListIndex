#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#define ERR_EXIT(m) \
    do \
    { \
        perror(m); \
        exit(EXIT_FAILURE); \
    } while(0)

int main(void)
{
    int fd;
    int ret;
    fd = open("/mnt/episode/test/hole.txt",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd == -1)
        ERR_EXIT("open error");
    write(fd,"hello",5);
//写入数据
//获取时间

//    ret = lseek(fd,1024*1024*1024,SEEK_CUR);
   ret = lseek64(fd,1024*1024*1024*1024*4,0);//游标移动到4TB 

    if(ret == -1)
        ERR_EXIT("lseek error");
    write(fd,"world",5);
    //写入索引
    close(fd);
    return 0;
}
