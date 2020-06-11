#include <stdio.h>

void main(){
  char a[10] ="000010007";
  int pos = 1;
  unsigned int b  = (unsigned int*)(a+pos);
printf("b=%x, %p, a+pos:%p",b, &b,a+pos);
}
