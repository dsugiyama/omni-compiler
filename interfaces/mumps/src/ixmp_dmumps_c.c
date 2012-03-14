#include "stdlib.h"
#include "dmumps_c.h"
#include "xmp.h"

void ixmp_dmumps_c(DMUMPS_STRUC_C *id, xmp_desc_t dirn, xmp_desc_t djcn, xmp_desc_t da){

  int irn_size;
  int jcn_size;
  int a_size;
  void *jcn_laddr;
  void *irn_laddr;
  void *a_laddr;

  xmp_array_lsize(dirn,&irn_size);
  xmp_array_lsize(djcn,&jcn_size);
  xmp_array_lsize(da,&a_size);
  if (irn_size == jcn_size && jcn_size == a_size) {
     id->nz_loc=irn_size;
  }else{
     exit(1);
  }

  xmp_array_laddr(dirn,&irn_laddr);
  xmp_array_laddr(djcn,&jcn_laddr);
  xmp_array_laddr(da,&a_laddr);
  id->irn_loc = *(void **)irn_laddr;
  id->jcn_loc = *(void **)jcn_laddr;
  id->a_loc = *(void **)a_laddr;
    
  dmumps_c(id);

}