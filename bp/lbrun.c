#include <stdio.h>
#include <stdlib.h>
#include "backend/lb_parse.h"
int main(int c,char**v){FILE*f=fopen(v[1],"rb");static char b[200000];size_t n=fread(b,1,sizeof b-1,f);b[n]=0;static lb_line_t l[LB_LINES_MAX];int k=lb_parse_html(b,l,LB_LINES_MAX);printf("%d lines\n",k);for(int i=0;i<k&&i<(c>2?atoi(v[2]):12);i++)printf("%d|%s\n",l[i].kind,l[i].text);return 0;}
