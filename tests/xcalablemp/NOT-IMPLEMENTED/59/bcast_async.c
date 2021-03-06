/*testp074.c*/
/*task$B;X<(J8$H(Bbcast$B;X<(J8$NAH$_9g$o$;(B*/
#include<xmp.h>
#include<mpi.h>
#include<stdio.h>     
static const int N=1000;
#pragma xmp nodes p(*)
#pragma xmp template t(0:N-1)
#pragma xmp distribute t(block) onto p
int procs,w;
int a[N][N],sa,ansa;
double b[N][N],sb,ansb;
float c[N][N],sc,ansc;
#pragma xmp align a[*][i] with t(i)
#pragma xmp align b[i][*] with t(i)
#pragma xmp align c[*][i] with t(i)
char *result;
int i,j;
int main(void){

   if(xmp_num_nodes() < 4){
      printf("%s\n","You have to this program by more than 4nodes.");
   }

#pragma xmp loop on t(i)
   for(i=0;i<N;i++){
      for(j=0;j<N;j++){
         a[i][j] = j/10*N+i/10;
         c[i][j] = (float)(j*N+i);
      }
   }

   for(i=0;i<N;i++){
#pragma xmp loop on t(i)
      for(j=0;j<N;j++){
         b[i][j] = (double)(j*N+i);
      }
   }

   sa = 0;
   sb = 0.0;
   sc = 0.0;
   procs = xmp_num_nodes();
   if(N%procs == 0){
      w = N/procs;
   }else{
      w = N/procs+1;
   }

#pragma xmp task on p(2:3)
   {
#pragma xmp loop on t(i)
      for(i=w;i<2*w;i++){
         for(j=0;j<N;j++){
            sa = sa+a[i][j];
         }
      }
      for(j=0;j<N;j++){
#pragma loop on t(i)
         for(i=w;i<3*w;i++){
            sb = sb+b[i][j];
         }
      }
#pragma xmp bcast (sa) from p(3) async(1)
#pragma xmp bcast (sb) from p(2) async(2)
#pragma xmp bcast (sc) from p(3) async(3)
#pragma xmp wait_async(1)
#pragma xmp wait_async(2)
#pragma xmp wait_async(3)
   }
 
   result = "OK";
   if(xmp_node_num()==2||xmp_node_num()==3){
      ansa = 0;
      ansb = 0.0;
      ansc = 0.0;
      for(j=2*w;j<3*w;j++){
         for(i=0;i<N;i++){
            ansa = ansa+j/10*N+i/10;
            ansc = ansc+(float)(j*N+i);
         }
      }
      for(j=0;j<N;j++){
         for(i=w;i<2*w;i++){
            ansb = ansb+(double)(j/10*N+i/10);
         }
      }
      if(sa!=ansa||sb!=ansb||sb!=ansc){
         result = "NG";
      }
   }else{
      if(sa!=0||sb!=0.0||sb!=0.0){
         result = "NG";
      }
   }  
     
   printf("%d %s %s\n",xmp_node_num(),"testp074.c",result);
   return 0;
}    
         
      
   
