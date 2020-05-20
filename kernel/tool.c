#include "episode.h"
//#include <stdlib.h>
#include <linux/timekeeping32.h>
//#include <string.h>
//#include <ctype.h>

/*从字符串的左边截取n个字符*/
void left(char *dst,char *src, int n)
{
    char *p = src;
    char *q = dst;
    int len=0;
    if(!src)
    {
        printk("source string is null\n");
        dst = NULL;
        return ;
    }
    while(src[len]){
        len++;
    } 
     
    if(n>len) n = len;
    /*p += (len-n);*/   /*从右边第n个字符开始*/
    while(n--) *(q++) = *(p++);
    *(q++)='\0'; /*有必要吗？很有必要*/
    //return dst;
}

/*从字符串的中间截取n个字符*/
void mid(char *dst,char *src, int n,int m) /*n为长度，m为位置*/
{
    char *p = src;
    char *q = dst;
    //int len = strlen(src);
    int len = 0;
    if(!src)
    {
        printk("source string is null\n");
        dst = NULL;
        return ;
    }
    while(src[len]){
        len++;
    } 
    if(n>len) n = len-m;    /*从第m个到最后*/
    if(m<0) m=0;    /*从第一个开始*/
    if(m>len) return NULL;
    p += m;
    while(n--) *(q++) = *(p++);
    *(q++)='\0'; /*有必要吗？很有必要*/
    //return dst;
}

/*从字符串的右边截取n个字符*/
void right(char *dst,char *src, int n)
{
    char *p = src;
    char *q = dst;
    //int len = strlen(src);
    int len = 0;
    if(!src)
    {
        printk("source string is null\n");
        dst = NULL;
        return ;
    }
    while(src[len]){
        len++;
    } 
    if(n>len) n = len;
    p += (len-n);   /*从右边第n个字符开始*/
    while(*(q++) = *(p++));
    //return dst;
}
int getCurrentTime(void ){
  struct timeval tv;
  do_gettimeofday(&tv);
  //gettimeofday(&tv, NULL);    //linux下该函数在sys/time.h头文件中  
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/**
 * 这样做的好处是，传进来的是num的一个副本，在函数调用之后，num并不会改变。
 */
void int32tochar(int32_t num, char ch[sizeof(int32_t)])
{
    int i;
    for( i=0;i<sizeof(int32_t);i++){
        //printf("当前b=%d, 字符b = %c \n",num,(char)num);
        ch[sizeof(int32_t)-1-i] =(char)num; //强制转化 不显示数据
        num = num>>8;//右移8位后，可能都变成0了，0强制转为字符，没有显示，且是字符串的结尾标志   
    }    
}
void chartoint32(int32_t *num, char ch[sizeof(int32_t)])
{

}



//整数的各位数字加‘0’转换为char型并保存到字符数组中                                                                                           
int itoa(int n, char s[])
{
    int i;
    int j;
    int sign;

    sign = n;    //记录符号
    if(sign < 0)
    {
       n = -n;  //变为正数处理 
    }

    i = 0;
    do{
        s[i++] = n % 10 + '0';  //取下一个数字
    }while((n /= 10) > 0);

    if(sign < 0 )
    {
        s[i++] = '-';
        s[i] = '\0';
    }

    for(j = i; j >= 0; j-- )
    {
        printk("%c \r\n", s[j]);
    }
    return 0;
}
