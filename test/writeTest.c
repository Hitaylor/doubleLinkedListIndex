#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
void main(){
	char *name = "/mnt/episode/t2.txt";

	int fd;
	int loopnum=3;
	for(int i=0;i<loopnum;i++){
		printf("loopnum = %d\n",i);
		fd =open(name,O_CREAT|O_RDWR|O_APPEND|O_DIRECT,0666);
		if(fd<0) printf("create file failed!");
		
		int32_t len1=7,len2=5;
		char buf1[]="abdcdef", buf2[]="mnbvc";
		int32_t a=len1, b=len2;

		char *content= malloc(1024);
		char  lench[sizeof(a)] ;
		//lench = itoa(len1,lench,10);
		for(int j=0;j<sizeof(a);j++)
		{
			lench[sizeof(a)-1-j]=(char)len1;
			len1 = len1>>8;
		}
		printf("begin to memcpy()!\n");
		memcpy(content,lench,sizeof(lench));
		
		for(int j=0;j<sizeof(a);j++) printf("%d",content[j]);
		printf("\n------------\n");

		memcpy(content+sizeof(a),buf1,a);
		for(int j=sizeof(a);j<a+sizeof(a);j++) printf("%c",content[j]);
		printf("\n------------\n");

	for(int j=0;j<sizeof(b);j++)
		{
			lench[sizeof(b)-1-j]=(char)len2;
			len2 = len2>>8;
		}
		printf("begin to memcpy(len2)!\n");
		//lench = itoa(len2,lench,10);
		memcpy(content+sizeof(a)+a,lench,sizeof(lench));
		for(int j=sizeof(a)+a;j<sizeof(a)+a+sizeof(b);j++) printf("%d",content[j]);
		printf("\n------------\n");
		memcpy(content+sizeof(a)+a+sizeof(b),buf2,b);

		for(int j=sizeof(a)+a+sizeof(b);j<sizeof(a)+a+sizeof(b)+b;j++)  printf("%c",content[j]);
		printf("\n");

	  
		
		//content = memalign(512,512*2);
		//memcpy(content,buf,sizeof(buf));
	int retnum =	write(fd,content,a+b+sizeof(a)+sizeof(b));
		close(fd);
		printf("write %d bytes succeed!\n",retnum);
		free(content);
	}

}
