#include <stdio.h>
#include <xmp.h>

int n=5;
double a[n][n][n][n][n][n][n],b[n][n][n][n][n][n][n];
#pragma xmp nodes p(2)
#pragma xmp template tx(0:n-1)
#pragma xmp template ty(0:n-1)
#pragma xmp distribute tx(block) onto p
#pragma xmp distribute ty(cyclic) onto p
#pragma xmp align a[*][*][*][*][*][*][i] with tx(i)
#pragma xmp align b[*][i][*][*][*][*][*] with ty(i)

int main(){

  int i0,i1,i2,i3,i4,i5,i6;
  int myrank;
  double err;

  myrank=xmp_node_num();

  for(i0=0;i0<n;i0++){
    for(i1=0;i1<n;i1++){
      for(i2=0;i2<n;i2++){
        for(i3=0;i3<n;i3++){
          for(i4=0;i4<n;i4++){
            for(i5=0;i5<n;i5++){
#pragma xmp loop (i6) on tx(i6)
              for(i6=0;i6<n;i6++){
                a[i0][i1][i2][i3][i4][i5][i6]=i0+i1+i2+i3+i4+i5+i6+2;
              }
            }
          }
        }
      }
    }
  }

  for(i0=0;i0<n;i0++){
#pragma xmp loop (i1) on ty(i1)
    for(i1=0;i1<n;i1++){
      for(i2=0;i2<n;i2++){
        for(i3=0;i3<n;i3++){
          for(i4=0;i4<n;i4++){
            for(i5=0;i5<n;i5++){
              for(i6=0;i6<n;i6++){
                b[i0][i1][i2][i3][i4][i5][i6]=0;
              }
            }
          }
        }
      }
    }
  }

#pragma xmp gmove
  b[0:n][0:n][0:n][0:n][0:n][0:n][0:n]=a[0:n][0:n][0:n][0:n][0:n][0:n][0:n];

  err=0.0;
  for(i0=0;i0<n;i0++){
#pragma xmp loop (i1) on ty(i1)
    for(i1=0;i1<n;i1++){
      for(i2=0;i2<n;i2++){
        for(i3=0;i3<n;i3++){
          for(i4=0;i4<n;i4++){
            for(i5=0;i5<n;i5++){
              for(i6=0;i6<n;i6++){
                err=err+fabs(b[i0][i1][i2][i3][i4][i5][i6]-(i0+i1+i2+i3+i4+i5+i6+2));
              }
            }
          }
        }
      }
    }
  }

  if (myrank ==1){
    printf("max error=%f\n",err);
  }

  return 0;

}

