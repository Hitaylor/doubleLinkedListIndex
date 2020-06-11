#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc.h>
#include <string.h>

void main(){
	char *name = "/mnt/episode/idx1.txt";

	int fd;
	int loopnum=10;
	for(int i=0;i<loopnum;i++){
		printf("loopnum = %d\n",i);
		fd =open(name,O_CREAT|O_RDWR|O_APPEND|O_DIRECT,0600);
		if(fd<0) printf("create file failed!");
		int32_t off=0;
		char buf1[]="abdc123",buf2[]="qwertyui",buf3[]="qazwsxedcvfr4321";
		int32_t len1=sizeof(buf1)-1,len2=sizeof(buf2)-1,len3=sizeof(buf3)-1;
			
	char *content;	

	content = memalign(512,512*2);
	memset(content,0,512*2);
	
	memcpy(content+off,&len1,sizeof(len1));
		off +=sizeof(len1);
	memcpy(content+off,buf1,len1);
		off +=len1;
	memcpy(content+off,&len2,sizeof(len2));
		off +=sizeof(len2);
	memcpy(content+off,buf2,len2);
		off +=len2;
	memcpy(content+off,&len3,sizeof(len3));
		off +=sizeof(len3);
	memcpy(content+off,buf3,len3);
		off =0;
/*
		for(int j=0;j<50;j++){
		    if(content[j]<123 && content[j]>96) printf("content[%d]=%d, =%c\n",j,content[j],content[j]);
                    else if(content[j]<91 && content[j]>64) printf("content[%d]=%d, =%c\n",j,content[j],content[j]);
                    else if(content[j]<58 && content[j]>47) printf("content[%d]=%d, =%c\n",j,content[j],content[j]);
               	    else printf("content[%d]=%d\n",j,content[j]);
		}

		printf("\n");
*/		int retnum = write(fd,content,512*2);
		printf("write bytes :%d\n",retnum);
		close(fd);
		
		printf("write succeed!\n");
		free(content);
	}
}