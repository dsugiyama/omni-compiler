#include "xmpf_internal.h"

void *_XMP_get_array_addr(_XMP_array_t *a, int *gidx)
{
  int ndims = a->dim;
  void *ret = a->array_addr_p;
  //xmpf_dbg_printf("ret = %p\n", ret);
  //xmpf_dbg_printf("a->array_addr_p = %p\n", a->array_addr_p);

  _XMP_ASSERT(a->is_allocated);

  for (int i = 0; i < ndims; i++){

    _XMP_array_info_t *ai = &(a->info[i]);
    _XMP_template_info_t *ti = &(a->align_template->info[ai->align_template_index]);
    _XMP_template_chunk_t *tc = &(a->align_template->chunk[ai->align_template_index]);
    int lidx;
    int offset;
    int glb, t_lb, np, w;
    int l_shadow;
    size_t type_size = a->type_size;

    switch (ai->align_manner){

      case _XMP_N_ALIGN_NOT_ALIGNED:
	lidx = gidx[i] - ai->ser_lower;
	break;

      case _XMP_N_ALIGN_DUPLICATION:
	lidx = gidx[i] - ai->ser_lower;
	break;

      case _XMP_N_ALIGN_BLOCK:
	// par_lower is the index of the lower bound of the local section.
	glb = ai->par_lower;
	l_shadow = ai->shadow_size_lo;
	lidx = gidx[i] - glb + l_shadow;
	break;

      case _XMP_N_ALIGN_CYCLIC:
	// assumed that even a cyclic array is distributed equally
	offset = ai->align_subscript;
	t_lb = ti->ser_lower;
	np = ai->par_stride;
	lidx = (gidx[i] + offset - t_lb) / np;
	break;

      case _XMP_N_ALIGN_BLOCK_CYCLIC:
	// assumed that even a cyclic array is distributed equally
	offset = ai->align_subscript;
	t_lb = ti->ser_lower;
	np = ai->par_stride;
	w = tc->par_stride;
	lidx = w * ((gidx[i] + offset - t_lb) / (np * w))
	     + ((gidx[i] + offset - t_lb) % w);
	break;

      default:
	_XMP_fatal("_XMP_get_array_addr: unknown align_manner");
    }

    //xmpf_dbg_printf("a->array_addr_p = %p\n", a->array_addr_p);
    //xmpf_dbg_printf("ret = %p, lidx = %d, ai->dim_acc = %d\n", ret, lidx, ai->dim_acc);
    ret = (char *)ret + lidx * ai->dim_acc * type_size;
  }

  return ret;

}


//
// s = ga(i)
//
void _XMPF_gmove_scalar_garray__(void *scalar, _XMP_gmv_desc_t *gmv_desc_rightp)
{
  _XMP_array_t *array = gmv_desc_rightp->a_desc;
  int type_size = array->type_size;
  void *src_addr = NULL;
  _XMP_nodes_t *exec_nodes = _XMP_get_execution_nodes();

  int ndims = gmv_desc_rightp->ndims;
  int ridx[ndims];

  for (int i = 0; i < ndims; i++) {
    ridx[i] = gmv_desc_rightp->lb[i];
  }

  //  xmpf_dbg_printf("gmove : a->array_addr_p = %p\n", array->array_addr_p);
/*   if (_XMP_IS_SINGLE){ */
/*     memcpy(scalar, src_addr, type_size); */
/*     return; */
/*   } */

  int root_rank = _XMP_calc_gmove_array_owner_linear_rank_SCALAR(array, ridx);
  xmpf_dbg_printf("root_rank = %d\n", root_rank);

  if (root_rank == exec_nodes->comm_rank){
    // I am the root.
    src_addr = _XMP_get_array_addr(array, ridx);
    xmpf_dbg_printf("src_addr = %p\n", src_addr);
  }

  // broadcast
  _XMP_gmove_bcast_SCALAR(scalar, src_addr, type_size, root_rank);
}

//
// ga(i) = s
//
void _XMPF_gmove_garray_scalar__(_XMP_gmv_desc_t *gmv_desc_leftp, void *scalar)
{
  _XMP_array_t *array = gmv_desc_leftp->a_desc;
  int type_size = array->type_size;
  void *dst_addr = NULL;
  _XMP_nodes_t *exec_nodes = _XMP_get_execution_nodes();

  int ndims = gmv_desc_leftp->ndims;
  int lidx[ndims];

  for (int i = 0; i < ndims; i++) {
    lidx[i] = gmv_desc_leftp->lb[i];
  }

  int owner_rank = _XMP_calc_gmove_array_owner_linear_rank_SCALAR(array, lidx);

  if (owner_rank == exec_nodes->comm_rank){
    // I am the owner.
    dst_addr = _XMP_get_array_addr(array, lidx);
    memcpy(dst_addr, scalar, type_size);
  }

}

//
// ga(:) = gb(:)
//
void _XMPF_gmove_garray_garray(_XMP_gmv_desc_t *gmv_desc_leftp,
			       _XMP_gmv_desc_t *gmv_desc_rightp)
{
  _XMP_array_t *dst_array = gmv_desc_leftp->a_desc;
  _XMP_array_t *src_array = gmv_desc_rightp->a_desc;

  _XMP_ASSERT(src_array->type == type);
  _XMP_ASSERT(src_array->type_size == type_size);

  unsigned long long gmove_total_elmts = 0;

  // get dst info
  unsigned long long dst_total_elmts = 1;
  int dst_dim = dst_array->dim;
  int dst_l[dst_dim], dst_u[dst_dim], dst_s[dst_dim];
  unsigned long long dst_d[dst_dim];
  for (int i = 0; i < dst_dim; i++) {
    dst_l[i] = gmv_desc_leftp->lb[i];
    dst_u[i] = gmv_desc_leftp->ub[i];
    dst_s[i] = gmv_desc_leftp->st[i];
    dst_d[i] = dst_array->info[i].dim_acc;
    _XMP_normalize_array_section(&(dst_l[i]), &(dst_u[i]), &(dst_s[i]));
    dst_total_elmts *= _XMP_M_COUNT_TRIPLETi(dst_l[i], dst_u[i], dst_s[i]);
  }

  // get src info
  unsigned long long src_total_elmts = 1;
  int src_dim = src_array->dim;
  int src_l[src_dim], src_u[src_dim], src_s[src_dim];
  unsigned long long src_d[src_dim];
  for (int i = 0; i < src_dim; i++) {
    src_l[i] = gmv_desc_rightp->lb[i];
    src_u[i] = gmv_desc_rightp->ub[i];
    src_s[i] = gmv_desc_rightp->st[i];
    src_d[i] = src_array->info[i].dim_acc;
    _XMP_normalize_array_section(&(src_l[i]), &(src_u[i]), &(src_s[i]));
    src_total_elmts *= _XMP_M_COUNT_TRIPLETi(src_l[i], src_u[i], src_s[i]);
  }

  if (dst_total_elmts != src_total_elmts) {
    _XMP_fatal("bad assign statement for gmove");
  } else {
    gmove_total_elmts = dst_total_elmts;
  }

  _XMP_gmove_array_array_common(gmv_desc_leftp, gmv_desc_rightp, dst_l, dst_u, dst_s, dst_d, src_l, src_u, src_s, src_d);

}

//
// ga(:) = la(:)
//
void _XMPF_gmove_garray_larray(_XMP_gmv_desc_t *gmv_desc_leftp,
			       _XMP_gmv_desc_t *gmv_desc_rightp)
{
  _XMP_array_t *dst_array = gmv_desc_leftp->a_desc;
  _XMP_array_t *src_array = gmv_desc_rightp->a_desc;

  int type = dst_array->type;
  size_t type_size = dst_array->type_size;

  _XMP_ASSERT(src_array->type == type);
  _XMP_ASSERT(src_array->type_size == type_size);

  if (!dst_array->is_allocated) return;

  // get dst info
  unsigned long long dst_total_elmts = 1;
  void *dst_addr = dst_array->array_addr_p;
  int dst_dim = dst_array->dim;
  int dst_l[dst_dim], dst_u[dst_dim], dst_s[dst_dim];
  unsigned long long dst_d[dst_dim];
  for (int i = 0; i < dst_dim; i++) {
    dst_l[i] = gmv_desc_leftp->lb[i];
    dst_u[i] = gmv_desc_leftp->ub[i];
    dst_s[i] = gmv_desc_leftp->st[i];
    dst_d[i] = dst_array->info[i].dim_acc;
    _XMP_normalize_array_section(&(dst_l[i]), &(dst_u[i]), &(dst_s[i]));
    dst_total_elmts *= _XMP_M_COUNT_TRIPLETi(dst_l[i], dst_u[i], dst_s[i]);
  }

  // get src info
  unsigned long long src_total_elmts = 1;
  void *src_addr = src_array->array_addr_p;
  int src_dim = src_array->dim;
  int src_l[src_dim], src_u[src_dim], src_s[src_dim];
  unsigned long long src_d[src_dim];
  for (int i = 0; i < src_dim; i++) {
    src_l[i] = gmv_desc_rightp->lb[i];
    src_u[i] = gmv_desc_rightp->ub[i];
    src_s[i] = gmv_desc_rightp->st[i];
    src_d[i] = src_array->info[i].dim_acc;
    _XMP_normalize_array_section(&(src_l[i]), &(src_u[i]), &(src_s[i]));
    src_total_elmts *= _XMP_M_COUNT_TRIPLETi(src_l[i], src_u[i], src_s[i]);
  }

  if (dst_total_elmts != src_total_elmts) {
    _XMP_fatal("bad assign statement for gmove");
  }

  if (_XMP_IS_SINGLE) {
    for (int i = 0; i < dst_dim; i++) {
      _XMP_gtol_array_ref_triplet(dst_array, i, &(dst_l[i]), &(dst_u[i]), &(dst_s[i]));
    }

    _XMP_gmove_localcopy_ARRAY(type, type_size,
                               dst_addr, dst_dim, dst_l, dst_u, dst_s, dst_d,
                               src_addr, src_dim, src_l, src_u, src_s, src_d);
    return;
  }

  // calc index ref
  int src_dim_index = 0;
  unsigned long long dst_buffer_elmts = 1;
  unsigned long long src_buffer_elmts = 1;
  for (int i = 0; i < dst_dim; i++) {
    int dst_elmts = _XMP_M_COUNT_TRIPLETi(dst_l[i], dst_u[i], dst_s[i]);
    if (dst_elmts == 1) {
      if(!_XMP_check_gmove_array_ref_inclusion_SCALAR(dst_array, i, dst_l[i])) {
        return;
      }
    } else {
      dst_buffer_elmts *= dst_elmts;

      int src_elmts;
      do {
        src_elmts = _XMP_M_COUNT_TRIPLETi(src_l[src_dim_index], src_u[src_dim_index], src_s[src_dim_index]);
        if (src_elmts != 1) {
          break;
        } else if (src_dim_index < src_dim) {
          src_dim_index++;
        } else {
          _XMP_fatal("bad assign statement for gmove");
        }
      } while (1);

      if (_XMP_calc_global_index_HOMECOPY(dst_array, i,
                                          &(dst_l[i]), &(dst_u[i]), &(dst_s[i]),
                                          &(src_l[src_dim_index]), &(src_u[src_dim_index]), &(src_s[src_dim_index]))) {
        src_buffer_elmts *= src_elmts;
        src_dim_index++;
      } else {
        return;
      }
    }

    _XMP_gtol_array_ref_triplet(dst_array, i, &(dst_l[i]), &(dst_u[i]), &(dst_s[i]));
  }

  for (int i = src_dim_index; i < src_dim; i++) {
    src_buffer_elmts *= _XMP_M_COUNT_TRIPLETi(src_l[i], src_u[i], src_s[i]);
  }

  // alloc buffer
  if (dst_buffer_elmts != src_buffer_elmts) {
    _XMP_fatal("bad assign statement for gmove");
  }

  void *buffer = _XMP_alloc(dst_buffer_elmts * type_size);
  _XMP_pack_array(buffer, src_addr, type, type_size, src_dim, src_l, src_u, src_s, src_d);
  _XMP_unpack_array(dst_addr, buffer, type, type_size, dst_dim, dst_l, dst_u, dst_s, dst_d);
  _XMP_free(buffer);

}


//
// la(:) = ga(:)
//
void _XMPF_gmove_larray_garray(_XMP_gmv_desc_t *gmv_desc_leftp,
			       _XMP_gmv_desc_t *gmv_desc_rightp)
{
  _XMP_array_t *src_array = gmv_desc_rightp->a_desc;

  int type = 0;
  size_t type_size = 0;

  unsigned long long gmove_total_elmts = 0;

  // get dst info
  unsigned long long dst_total_elmts = 1;
  void *dst_addr = gmv_desc_rightp->local_data;
  int dst_dim = gmv_desc_leftp->ndims;
  int dst_l[dst_dim], dst_u[dst_dim], dst_s[dst_dim];
  unsigned long long dst_d[dst_dim];
  for (int i = 0; i < dst_dim; i++) {
    dst_l[i] = gmv_desc_leftp->lb[i];
    dst_u[i] = gmv_desc_leftp->ub[i];
    dst_s[i] = gmv_desc_leftp->st[i];
    if (i == 0){
      dst_d[i] =1;
    }else{
      dst_d[i] = dst_d[i-1]*(gmv_desc_leftp->a_ub[i] - gmv_desc_leftp->a_lb[i]+1);
    }
    _XMP_normalize_array_section(&(dst_l[i]), &(dst_u[i]), &(dst_s[i]));
    dst_total_elmts *= _XMP_M_COUNT_TRIPLETi(dst_l[i], dst_u[i], dst_s[i]);
  }

  // get src info
  unsigned long long src_total_elmts = 1;
  void *src_addr = src_array->array_addr_p;
  int src_dim = src_array->dim;
  int src_l[src_dim], src_u[src_dim], src_s[src_dim];
  unsigned long long src_d[src_dim];
  for (int i = 0; i < src_dim; i++) {
    src_l[i] = gmv_desc_rightp->lb[i];
    src_u[i] = gmv_desc_rightp->ub[i];
    src_s[i] = gmv_desc_rightp->st[i];
    src_d[i] = src_array->info[i].dim_acc;
    _XMP_normalize_array_section(&(src_l[i]), &(src_u[i]), &(src_s[i]));
    src_total_elmts *= _XMP_M_COUNT_TRIPLETi(src_l[i], src_u[i], src_s[i]);
  }

  if (dst_total_elmts != src_total_elmts) {
    _XMP_fatal("bad assign statement for gmove");
  } else {
    gmove_total_elmts = dst_total_elmts;
  }

  _XMP_gmove_array_array_common(gmv_desc_leftp, gmv_desc_rightp, dst_l, dst_u, dst_s, dst_d, src_l, src_u, src_s, src_d);

  int iflag =0;
  if (iflag==1){
  if (_XMP_IS_SINGLE) {
    for (int i = 0; i < src_dim; i++) {
      _XMP_gtol_array_ref_triplet(src_array, i, &(src_l[i]), &(src_u[i]), &(src_s[i]));
    }

    _XMP_gmove_localcopy_ARRAY(type, type_size,
                               dst_addr, dst_dim, dst_l, dst_u, dst_s, dst_d,
                               src_addr, src_dim, src_l, src_u, src_s, src_d);
    return;
  }

  _XMP_nodes_t *exec_nodes = _XMP_get_execution_nodes();
  _XMP_ASSERT(exec_nodes->is_member);

  _XMP_nodes_t *array_nodes = src_array->array_nodes;
  int array_nodes_dim = array_nodes->dim;
  int array_nodes_ref[array_nodes_dim];
  for (int i = 0; i < array_nodes_dim; i++) {
    array_nodes_ref[i] = 0;
  }

  int dst_lower[dst_dim], dst_upper[dst_dim], dst_stride[dst_dim];
  int src_lower[src_dim], src_upper[src_dim], src_stride[src_dim];
  do {
    for (int i = 0; i < dst_dim; i++) {
      dst_lower[i] = dst_l[i]; dst_upper[i] = dst_u[i]; dst_stride[i] = dst_s[i];
    }

    for (int i = 0; i < src_dim; i++) {
      src_lower[i] = src_l[i]; src_upper[i] = src_u[i]; src_stride[i] = src_s[i];
    }

    if (_XMP_calc_global_index_BCAST(dst_dim, dst_lower, dst_upper, dst_stride,
                                     src_array, array_nodes_ref, src_lower, src_upper, src_stride)) {
      int root_rank = _XMP_calc_linear_rank_on_target_nodes(array_nodes, array_nodes_ref, exec_nodes);
      if (root_rank == (exec_nodes->comm_rank)) {
        for (int i = 0; i < src_dim; i++) {
          _XMP_gtol_array_ref_triplet(src_array, i, &(src_lower[i]), &(src_upper[i]), &(src_stride[i]));
        }
      }

      gmove_total_elmts -= _XMP_gmove_bcast_ARRAY(dst_addr, dst_dim, dst_lower, dst_upper, dst_stride, dst_d,
                                                  src_addr, src_dim, src_lower, src_upper, src_stride, src_d,
                                                  type, type_size, root_rank);

      _XMP_ASSERT(gmove_total_elmts >= 0);
      if (gmove_total_elmts == 0) {
        return;
      }
    }
  } while (_XMP_get_next_rank(array_nodes, array_nodes_ref));
  }
}


/* gmove sequence:
 * For global array,
 *  CALL xmp_gmv_g_alloc_(desc,XMP_DESC_a)
 *  CALL xmp_gmv_g_info(desc,#i_dim,kind,lb,ub,stride)
 * For local array
 *  CALL xmp_gmv_l_alloc_(desc,array,a_dim)
 *  CALL xmp_gmv_l_info(desc,#i_dim,a_lb,a_ub,kind,lb,ub,stride)
 *
 * kind = 2 -> ub, up, stride
 *        1 -> index
 *        0 -> all (:)
 * And, followed by:
 *  CALL xmp_gmv_do(left,right,collective(0)/in(1)/out(2))
 * Note: data type must be describe one of global side
 */

/* private final static int GMOVE_ALL   = 0; */
/* private final static int GMOVE_INDEX = 1; */
/* private final static int GMOVE_RANGE = 2; */
  
/* private final static int GMOVE_COLL   = 0; */
/* private final static int GMOVE_IN = 1; */
/* private final static int GMOVE_OUT = 2; */


void
xmpf_gmv_g_alloc__(_XMP_gmv_desc_t **gmv_desc, _XMP_array_t **a_desc)
{
  _XMP_gmv_desc_t *gp;
  _XMP_array_t *ap = *a_desc;
  int n = ap->dim;

  gp = (_XMP_gmv_desc_t *)malloc(sizeof(_XMP_gmv_desc_t));

  gp->kind = (int *)malloc(sizeof(int) * n);
  gp->lb = (int *)malloc(sizeof(int) * n);
  gp->ub = (int *)malloc(sizeof(int) * n);
  gp->st = (int *)malloc(sizeof(int) * n);
  
  if (!gp || !gp->kind || !gp->lb || !gp->st)
    _XMP_fatal("gmv_g_alloc: cannot alloc memory");

  gp->is_global = true;
  gp->ndims = n;
  gp->a_desc = ap;

  gp->local_data = NULL;
  gp->a_lb = NULL;
  gp->a_ub = NULL;

  *gmv_desc = gp;
}


void
xmpf_gmv_g_dim_info__(_XMP_gmv_desc_t **gmv_desc , int *i_dim,
		      int *kind, int *lb, int *ub, int *st)
{
  _XMP_gmv_desc_t *gp = *gmv_desc;
  int i = *i_dim;
  gp->kind[i] = *kind;
  gp->lb[i] = *lb;
  gp->ub[i] = *ub;
  gp->st[i] = *st;
}


void
xmpf_gmv_l_alloc__(_XMP_gmv_desc_t **gmv_desc , void *local_data, int *ndims)
{
    _XMP_gmv_desc_t *gp;
    int n = *ndims;

    gp = (_XMP_gmv_desc_t *)malloc(sizeof(_XMP_gmv_desc_t));

    gp->kind = (int *)malloc(sizeof(int) * n);
    gp->lb = (int *)malloc(sizeof(int) * n);
    gp->ub = (int *)malloc(sizeof(int) * n);
    gp->st = (int *)malloc(sizeof(int) * n);
    gp->a_lb = (int *)malloc(sizeof(int) * n);
    gp->a_ub = (int *)malloc(sizeof(int) * n);

    gp->is_global = false;
    gp->ndims = n;
    gp->a_desc = NULL;

    gp->local_data = local_data;

    *gmv_desc = gp;
}


void
xmpf_gmv_l_dim_info__(_XMP_gmv_desc_t **gmv_desc , int *i_dim, int *a_lb, int *a_ub,
		      int *kind, int *lb, int *ub, int *st)
{
  _XMP_gmv_desc_t *gp = *gmv_desc;
  int i = *i_dim;

  gp->a_lb[i] = *a_lb;
  gp->a_ub[i] = *a_ub;

  gp->kind[i] = *kind;
  gp->lb[i] = *lb;
  gp->ub[i] = *ub;
  gp->st[i] = *st;
}


void
xmpf_gmv_do__(_XMP_gmv_desc_t **gmv_desc_left, _XMP_gmv_desc_t **gmv_desc_right,
	   int *mode)
{
  _XMP_gmv_desc_t *gmv_desc_leftp = *gmv_desc_left;
  _XMP_gmv_desc_t *gmv_desc_rightp = *gmv_desc_right;

  if (gmv_desc_leftp->is_global && gmv_desc_rightp->is_global){
    _XMPF_gmove_garray_garray(gmv_desc_leftp, gmv_desc_rightp);
  }
  else if (gmv_desc_leftp->is_global && !gmv_desc_rightp->is_global){
    if (gmv_desc_rightp->ndims == 0){
      _XMPF_gmove_garray_scalar__(gmv_desc_leftp, gmv_desc_rightp->local_data);
    }
    else {
      _XMPF_gmove_garray_larray(gmv_desc_leftp, gmv_desc_rightp);
    }
  }
  else if (!gmv_desc_leftp->is_global && gmv_desc_rightp->is_global){
    if (gmv_desc_leftp->ndims == 0){
      _XMPF_gmove_scalar_garray__(gmv_desc_leftp->local_data, gmv_desc_rightp);
    }
    else {
      _XMPF_gmove_larray_garray(gmv_desc_leftp, gmv_desc_rightp);
    }
  }
  else {
    _XMP_fatal("gmv_do: both sides are local.");
  }

}
