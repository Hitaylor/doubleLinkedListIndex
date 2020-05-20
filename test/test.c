#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
//#include "episode.h"

void main(){
        int a=7,b=5;
        int len1=a, len2=b;
		char buf1[]="abdcdef", buf2[]="mnbvc";
        printf("len(buf1):%ld, len(buf2):%ld; sizeof(buf1):%ld, sizeof(buf2):%ld\n",strlen(buf1),strlen(buf2),sizeof(buf1),sizeof(buf2));

		//char content[100]={0};// = malloc(100);
		char * content =malloc(100);
        memcpy(content,&a,sizeof(a));
        for(int k=0;k<sizeof(a);k++) printf("%d",content[k]);
        printf("------------\n");
        int t;
        memcpy(&t,content,sizeof(t));
        printf("t = %d\n",t);

        char ch[4];
        for(int i=0;i<4;i++)
        {
            ch[4-1-i]=(char)a;
            a=a>>8;
            //a++;
        }
		printf("begin to memcpy()! sizeof(ch):%ld, sizeof(buf1):%ld, sizeof(buf2):%ld\n",sizeof(ch),sizeof(buf1),sizeof(buf2));
        printf("strlen(ch):%ld, strlen(buf1):%ld, strlen(buf2):%ld\n",strlen(ch),strlen(buf1),strlen(buf2));
		memcpy(content,ch,sizeof(ch));
		
        for(int i=0;i<4;i++)
         printf("%d",content[i]);
        printf("\n-------\n");

        memcpy(&content[sizeof(ch)],buf1,len1);
       // printf("目前 a=%d \n",a);
        //strcat(content,buf1);
        for(int i=4;i<4+len1;i++) 
            printf("i=%d, ch=%c \n",i,content[i]);
        printf("=========================\n");
		
        printf("begin to memcpy(len2)!\n");
		//lench = itoa(len2,lench,10);
        for(int i=0;i<4;i++){
            //printf("当前b=%d, 字符b = %c \n",b,(char)b);
            ch[4-1-i] =(char)b; //强制转化 不显示数据
            b = b>>8;//右移8位，都变成0了，0强制转为字符，没有显示  当前b=5, 字符b =  
        }
		memcpy(content+len1+4,ch,sizeof(ch));
        //strcat(content+4+a,ch);
        for(int i=4+len1;i<4+len1+4;i++) printf("%d",content[i]);
        printf("\n------------------\n");
		//strcat(content+a+4+4,buf2);
        memcpy(content+len1+8,buf2,len2);
        printf("sizeof(content)=%ld \n",sizeof(content));

		//for(int i=a+8;i<a+b+8;i++) //b都等于0了，for循环不执行
        for(int i=len1+8;i<len1+len2+8;i++) 
        printf("当前值 content[i],i=%d, ch = %c \n",i,content[i]);
		printf("game over \n");
      //  free(content);
}