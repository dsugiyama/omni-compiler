#include <stdio.h>
#include <stdlib.h>
#include <xmp.h>
#define N 8

int n=N;
int a[n][n][n][n];
#pragma xmp nodes p(2,2,2,2)
#pragma xmp template tx(0:n-1,0:n-1,0:n-1,0:n-1)
#pragma xmp distribute tx(cyclic(2),cyclic(2),cyclic(2),cyclic(2)) onto p
#pragma xmp align a[i0][i1][i2][i3] with tx(i0,i1,i2,i3)

int main(){

  int i0,i1,i2,i3,b[N][N][N][N],ierr=0;

#pragma xmp loop (i0,i1,i2,i3) on tx(i0,i1,i2,i3)
  for (i3=0;i3<n;i3++){
    for (i2=0;i2<n;i2++){
      for (i1=0;i1<n;i1++){
        for (i0=0;i0<n;i0++){
          a[i0][i1][i2][i3]=(i0+1)+(i1+1)+(i2+1)+(i3+1);
        }
      }
    }
  }

  for (i3=0;i3<n;i3++){
    for (i2=0;i2<n;i2++){
      for (i1=0;i1<n;i1++){
        for (i0=0;i0<n;i0++){
          b[i0][i1][i2][i3]=0;
        }
      }
    }
  }

#pragma xmp gmove
  b[1:4][1:4][1:4][1:4]=a[4:4][4:4][4:4][4:4];

  for (i3=1;i3<5;i3++){
    for (i2=1;i2<5;i2++){
      for (i1=1;i1<5;i1++){
        for (i0=1;i0<5;i0++){
          ierr=ierr+abs(b[i0][i1][i2][i3]-(i0+4)-(i1+4)-(i2+4)-(i3+4));
        }
      }
    }
  }

  int irank= xmp_node_num();
#pragma xmp reduction (+:ierr)
  if (irank == 1){
    printf("max error=%d\n",ierr);
  }
  return ierr;

}
