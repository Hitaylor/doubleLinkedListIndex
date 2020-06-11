#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc.h>
#include <string.h>


#define BUFSIZE 4096

void main()
{
  char *name = "/mnt/episode/idx1.txt";

  int fd;
  int loopnum = 1;
  for (int i = 0; i < loopnum; i++)
  {
    printf("loopnum = %d\n", i);
    fd = open(name,  O_RDONLY, 0600);
    if (fd < 0)
    {
      printf("open file failed!");
      exit(1);
    }
    int32_t size = 0;
    char buf[BUFSIZE];
    char buf1[] = "abdc123", buf2[] = "qwertyui", buf3[] = "qazwsxedcvfr4321";
    int32_t len1 = sizeof(buf1) - 1, len2 = sizeof(buf2) - 1, len3 = sizeof(buf3) - 1;

    lseek(fd, 0, SEEK_SET);
    if ((size = read(fd, buf, BUFSIZE)) < 0)
    {
      perror("read:");
      exit(1);
    }
    else
      printf("bytes read from file:%d\n", size);

   /* for (int j = 0; j < 1024; j++)
    {
      if (buf[j] < 123 && buf[j] > 96)
        printf("buf[%d]=%d, =%c\n", j, buf[j], buf[j]);
      else if (buf[j] < 91 && buf[j] > 64)
        printf("buf[%d]=%d, =%c\n", j, buf[j], buf[j]);
      else if (buf[j] < 58 && buf[j] > 47)
        printf("buf[%d]=%d, =%c\n", j, buf[j], buf[j]);
      else
        printf("buf[%d]=%d\n", j, buf[j]);
    }
    printf("\n");
    */
    for (int j = 0; j < BUFSIZE; j++)
    {

      if ((j+1)%16==0){
        printf("%02x \n",buf[j]);
      }else{
        printf("%02x ",buf[j]);
      }
    }
    printf("\n");
    
   //再读一次
    if ((size = read(fd, buf, BUFSIZE)) < 0)
    {
      perror("read:");
      exit(1);
    }
    else
      printf("bytes read from file:%d\n", size);
 for (int j = 0; j < BUFSIZE; j++)
    {

      if ((j+1)%16==0){
        printf("%02x \n",buf[j]);
      }else{
        printf("%02x ",buf[j]);
      }
    }

    printf("buf addr:%0x\n",buf);


    if (close(fd) < 0)
    {
      perror("close:");
      exit(1);
    }
    else
      printf("Close hello.c\n");
  }
  exit(0);
}