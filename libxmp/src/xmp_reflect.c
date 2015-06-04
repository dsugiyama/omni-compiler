#include "xmp_internal.h"
#include <string.h>

static void _XMP_reflect_sched(_XMP_array_t *a, int *lwidth, int *uwidth,
				int *is_periodic, int is_async);

#if !defined(OMNI_TARGET_CPU_KCOMPUTER) || !defined(K_RDMA_REFLECT)
static void _XMP_reflect_normal_sched_dim(_XMP_array_t *adesc, int target_dim,
					   int lwidth, int uwidth, int is_periodic);
static void _XMP_reflect_pcopy_sched_dim(_XMP_array_t *adesc, int target_dim,
					  int lwidth, int uwidth, int is_periodic);
static void _XMP_reflect_start(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic,
				int dummy);
static void _XMP_reflect_wait(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic);
#else
static void _XMP_reflect_rdma_sched_dim(_XMP_array_t *adesc, int target_dim,
					 int lwidth, int uwidth, int is_periodic);
static void _XMP_reflect_start(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic,
				 int tag);
static void _XMP_reflect_wait(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic);
#endif

//int _XMP_get_owner_pos_BLOCK(_XMP_array_t *a, int dim, int index);
int _XMP_get_owner_pos(_XMP_array_t *a, int dim, int index);

void _XMP_reflect_pack(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic);
void _XMP_reflect_unpack(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic);


//#define DBG 1

#include <stdio.h>
void xmp_dbg_printf(char *fmt,...)
{
  char buf[512];
  va_list args;

  va_start(args,fmt);
  vsprintf(buf,fmt,args);
  va_end(args);

  printf("[%d] %s",_XMP_world_rank, buf);
  fflush(stdout);
}

#ifdef _XMP_TIMING
double t0, t1;
#endif

extern int _xmp_reflect_pack_flag;

static int _xmpf_set_reflect_flag = 0;
static int _xmp_lwidth[_XMP_N_MAX_DIM] = {0};
static int _xmp_uwidth[_XMP_N_MAX_DIM] = {0};
static int _xmp_is_periodic[_XMP_N_MAX_DIM] = {0};


void _XMP_set_reflect__(_XMP_array_t *a, int dim, int lwidth, int uwidth,
		       int is_periodic)
{
  _xmpf_set_reflect_flag = 1;
  _xmp_lwidth[dim] = lwidth;
  _xmp_uwidth[dim] = uwidth;
  _xmp_is_periodic[dim] = is_periodic;
}

//double t0, t_sched = 0, t_start = 0, t_wait = 0;

void _XMP_reflect__(_XMP_array_t *a)
{
  _XMP_RETURN_IF_SINGLE;
  if (!a->is_allocated){
    _xmpf_set_reflect_flag = 0;
    return;
  }

  if (!_xmpf_set_reflect_flag){
    for (int i = 0; i < a->dim; i++){
      _XMP_array_info_t *ai = &(a->info[i]);
      _xmp_lwidth[i] = ai->shadow_size_lo;
      _xmp_uwidth[i] = ai->shadow_size_hi;
      _xmp_is_periodic[i] = 0;
    }
  }

  //  t0 = MPI_Wtime();
  _XMP_reflect_sched(a, _xmp_lwidth, _xmp_uwidth, _xmp_is_periodic, 0);
  //  t_sched = t_sched + (MPI_Wtime() - t0);

  //  t0 = MPI_Wtime();
  _XMP_reflect_start(a, _xmp_lwidth, _xmp_uwidth, _xmp_is_periodic, 0);
  //  t_start = t_start + (MPI_Wtime() - t0);

  //  t0 = MPI_Wtime();
  _XMP_reflect_wait(a, _xmp_lwidth, _xmp_uwidth, _xmp_is_periodic);
  //  t_wait = t_wait + (MPI_Wtime() - t0);

  _xmpf_set_reflect_flag = 0;
  for (int i = 0; i < a->dim; i++){
    _xmp_lwidth[i] = 0;
    _xmp_uwidth[i] = 0;
    _xmp_is_periodic[i] = 0;
  }

}


static void _XMP_reflect_sched(_XMP_array_t *a, int *lwidth, int *uwidth,
				int *is_periodic, int is_async)
{
  _XMP_TSTART(t0);
  for (int i = 0; i < a->dim; i++){

    _XMP_array_info_t *ai = &(a->info[i]);

    if (ai->shadow_type == _XMP_N_SHADOW_NONE){
      continue;
    }
    else if (ai->shadow_type == _XMP_N_SHADOW_NORMAL){

      _XMP_reflect_sched_t *reflect = ai->reflect_sched;

      if (lwidth[i] || uwidth[i]){

	_XMP_ASSERT(reflect);

	if (reflect->is_periodic == -1 /* not set yet */ ||
	    lwidth[i] != reflect->lo_width ||
	    uwidth[i] != reflect->hi_width ||
	    is_periodic[i] != reflect->is_periodic){

	  reflect->lo_width = lwidth[i];
	  reflect->hi_width = uwidth[i];
	  reflect->is_periodic = is_periodic[i];

#if !defined(OMNI_TARGET_CPU_KCOMPUTER) || !defined(K_RDMA_REFLECT)
	  if (_xmp_reflect_pack_flag && !is_async){
	    _XMP_reflect_pcopy_sched_dim(a, i, lwidth[i], uwidth[i], is_periodic[i]);
	  }
	  else {
	    _XMP_reflect_normal_sched_dim(a, i, lwidth[i], uwidth[i], is_periodic[i]);
	  }
#else
	  _XMP_reflect_rdma_sched_dim(a, i, lwidth[i], uwidth[i], is_periodic[i]);
#endif

	}
      }

    }
    else { /* _XMP_N_SHADOW_FULL */
      ;
    }
    
  }
  _XMP_TEND(xmptiming_.t_sched, t0);

}


#if !defined(OMNI_TARGET_CPU_KCOMPUTER) || !defined(K_RDMA_REFLECT)

//
// Reflect without RDMA
//

static void _XMP_reflect_normal_sched_dim(_XMP_array_t *adesc, int target_dim,
					   int lwidth, int uwidth, int is_periodic){

  if (lwidth == 0 && uwidth == 0) return;

  _XMP_array_info_t *ai = &(adesc->info[target_dim]);
  _XMP_array_info_t *ainfo = adesc->info;
  _XMP_ASSERT(ai->align_manner == _XMP_N_ALIGN_BLOCK);
  _XMP_ASSERT(ai->is_shadow_comm_member);

  if (lwidth > ai->shadow_size_lo || uwidth > ai->shadow_size_hi){
    _XMP_fatal("reflect width is larger than shadow width.");
  }

  _XMP_reflect_sched_t *reflect = ai->reflect_sched;

  int target_tdim = ai->align_template_index;
  _XMP_nodes_info_t *ni = adesc->align_template->chunk[target_tdim].onto_nodes_info;

  int ndims = adesc->dim;

  // 0-origin
  int my_pos = ni->rank;
  int lb_pos = _XMP_get_owner_pos(adesc, target_dim, ai->ser_lower);
  int ub_pos = _XMP_get_owner_pos(adesc, target_dim, ai->ser_upper);

  int lo_pos = (my_pos == lb_pos) ? ub_pos : my_pos - 1;
  int hi_pos = (my_pos == ub_pos) ? lb_pos : my_pos + 1;

  MPI_Comm *comm = adesc->align_template->onto_nodes->comm;
  int my_rank = adesc->align_template->onto_nodes->comm_rank;

  int lo_rank = my_rank + (lo_pos - my_pos) * ni->multiplier;
  int hi_rank = my_rank + (hi_pos - my_pos) * ni->multiplier;

  int type_size = adesc->type_size;

  void *lo_recv_buf = adesc->array_addr_p;
  void *lo_send_buf = adesc->array_addr_p;
  void *hi_recv_buf = adesc->array_addr_p;
  void *hi_send_buf = adesc->array_addr_p;

  //
  // setup MPI_data_type
  //

  int count = 0, blocklength = 0;
  long long stride = 0;

  if (_XMPF_running && (!_XMPC_running)){ /* for XMP/F */

    count = 1;
    blocklength = type_size;
    stride = ainfo[0].alloc_size * type_size;

    for (int i = ndims - 2; i >= target_dim; i--){
      count *= ainfo[i+1].alloc_size;
    }

    for (int i = 1; i <= target_dim; i++){
      blocklength *= ainfo[i-1].alloc_size;
      stride *= ainfo[i].alloc_size;
    }

  }
  else if ((!_XMPF_running) && _XMPC_running){ /* for XMP/C */

    count = 1;
    blocklength = type_size;
    stride = ainfo[ndims-1].alloc_size * type_size;

    for (int i = 1; i <= target_dim; i++){
      count *= ainfo[i-1].alloc_size;
    }

    for (int i = ndims - 2; i >= target_dim; i--){
      blocklength *= ainfo[i+1].alloc_size;
      stride *= ainfo[i].alloc_size;
    }

  }
  else {
    _XMP_fatal("cannot determin the base language.");
  }

  // for lower reflect

  if (reflect->datatype_lo != MPI_DATATYPE_NULL){
    MPI_Type_free(&reflect->datatype_lo);
  }

  MPI_Type_vector(count, blocklength * lwidth, stride,
		  MPI_BYTE, &reflect->datatype_lo);
  MPI_Type_commit(&reflect->datatype_lo);

  // for upper reflect

  if (reflect->datatype_hi != MPI_DATATYPE_NULL){
    MPI_Type_free(&reflect->datatype_hi);
  }

  MPI_Type_vector(count, blocklength * uwidth, stride,
		  MPI_BYTE, &reflect->datatype_hi);
  MPI_Type_commit(&reflect->datatype_hi);

  //
  // calculate base address
  //

  // for lower reflect

  if (lwidth){
      
    for (int i = 0; i < ndims; i++) {

      int lb_send, lb_recv, dim_acc;

      if (i == target_dim) {
	lb_send = ainfo[i].local_upper - lwidth + 1;
	lb_recv = ainfo[i].shadow_size_lo - lwidth;
      }
      else {
	// Note: including shadow area
	lb_send = 0;
	lb_recv = 0;
      }

      dim_acc = ainfo[i].dim_acc;

      lo_send_buf = (void *)((char *)lo_send_buf + lb_send * dim_acc * type_size);
      lo_recv_buf = (void *)((char *)lo_recv_buf + lb_recv * dim_acc * type_size);
      
    }

  }

  // for upper reflect

  if (uwidth){

    for (int i = 0; i < ndims; i++) {

      int lb_send, lb_recv, dim_acc;

      if (i == target_dim) {
	lb_send = ainfo[i].local_lower;
	lb_recv = ainfo[i].local_upper + 1;
      }
      else {
	// Note: including shadow area
	lb_send = 0;
	lb_recv = 0;
      }

      dim_acc = ainfo[i].dim_acc;

      hi_send_buf = (void *)((char *)hi_send_buf + lb_send * dim_acc * type_size);
      hi_recv_buf = (void *)((char *)hi_recv_buf + lb_recv * dim_acc * type_size);
      
    }

  }

  //
  // initialize communication
  //

  int src, dst;

  if (!is_periodic && my_pos == lb_pos){ // no periodic
    lo_rank = MPI_PROC_NULL;
  }

  if (!is_periodic && my_pos == ub_pos){ // no periodic
    hi_rank = MPI_PROC_NULL;
  }

  // for lower reflect

  if (lwidth){
    src = lo_rank;
    dst = hi_rank;
  }
  else {
    src = MPI_PROC_NULL;
    dst = MPI_PROC_NULL;
  }

  if (reflect->req[0] != MPI_REQUEST_NULL){
    MPI_Request_free(&reflect->req[0]);
  }
	
  if (reflect->req[1] != MPI_REQUEST_NULL){
    MPI_Request_free(&reflect->req[1]);
  }

  MPI_Recv_init(lo_recv_buf, 1, reflect->datatype_lo, src,
		_XMP_N_MPI_TAG_REFLECT_LO, *comm, &reflect->req[0]);
  MPI_Send_init(lo_send_buf, 1, reflect->datatype_lo, dst,
		_XMP_N_MPI_TAG_REFLECT_LO, *comm, &reflect->req[1]);

  // for upper reflect

  if (uwidth){
    src = hi_rank;
    dst = lo_rank;
  }
  else {
    src = MPI_PROC_NULL;
    dst = MPI_PROC_NULL;
  }

  if (reflect->req[2] != MPI_REQUEST_NULL){
    MPI_Request_free(&reflect->req[2]);
  }
	
  if (reflect->req[3] != MPI_REQUEST_NULL){
    MPI_Request_free(&reflect->req[3]);
  }

  MPI_Recv_init(hi_recv_buf, 1, reflect->datatype_hi, src,
		_XMP_N_MPI_TAG_REFLECT_HI, *comm, &reflect->req[2]);
  MPI_Send_init(hi_send_buf, 1, reflect->datatype_hi, dst,
		_XMP_N_MPI_TAG_REFLECT_HI, *comm, &reflect->req[3]);
}


static void _XMP_reflect_pcopy_sched_dim(_XMP_array_t *adesc, int target_dim,
					 int lwidth, int uwidth, int is_periodic){

  if (lwidth == 0 && uwidth == 0) return;

  _XMP_array_info_t *ai = &(adesc->info[target_dim]);
  _XMP_array_info_t *ainfo = adesc->info;
  _XMP_ASSERT(ai->align_manner == _XMP_N_ALIGN_BLOCK);
  _XMP_ASSERT(ai->is_shadow_comm_member);

  if (lwidth > ai->shadow_size_lo || uwidth > ai->shadow_size_hi){
    _XMP_fatal("reflect width is larger than shadow width.");
  }

  _XMP_reflect_sched_t *reflect = ai->reflect_sched;

  int target_tdim = ai->align_template_index;
  _XMP_nodes_info_t *ni = adesc->align_template->chunk[target_tdim].onto_nodes_info;

  int ndims = adesc->dim;

  // 0-origin
  int my_pos = ni->rank;
  int lb_pos = _XMP_get_owner_pos(adesc, target_dim, ai->ser_lower);
  int ub_pos = _XMP_get_owner_pos(adesc, target_dim, ai->ser_upper);

  int lo_pos = (my_pos == lb_pos) ? ub_pos : my_pos - 1;
  int hi_pos = (my_pos == ub_pos) ? lb_pos : my_pos + 1;

  MPI_Comm *comm = adesc->align_template->onto_nodes->comm;
  int my_rank = adesc->align_template->onto_nodes->comm_rank;

  int lo_rank = my_rank + (lo_pos - my_pos) * ni->multiplier;
  int hi_rank = my_rank + (hi_pos - my_pos) * ni->multiplier;

  int type_size = adesc->type_size;
  void *array_addr = adesc->array_addr_p;

  void *lo_send_array = NULL, *lo_recv_array = NULL;
  void *hi_send_array = NULL, *hi_recv_array = NULL;

/*   void *lo_send_buf = array_addr; */
/*   void *lo_recv_buf = array_addr; */
/*   void *hi_send_buf = array_addr; */
/*   void *hi_recv_buf = array_addr; */
  void *lo_send_buf = NULL;
  void *lo_recv_buf = NULL;
  void *hi_send_buf = NULL;
  void *hi_recv_buf = NULL;

  int lo_buf_size = 0;
  int hi_buf_size = 0;

  //
  // setup data_type
  //

  int count = 0, blocklength = 0;
  long long stride = 0;

  if (_XMPF_running && (!_XMPC_running)){ /* for XMP/F */

    count = 1;
    blocklength = type_size;
    stride = ainfo[0].alloc_size * type_size;

    for (int i = ndims - 2; i >= target_dim; i--){
      count *= ainfo[i+1].alloc_size;
    }

    for (int i = 1; i <= target_dim; i++){
      blocklength *= ainfo[i-1].alloc_size;
      stride *= ainfo[i].alloc_size;
    }

  }
  else if ((!_XMPF_running) && _XMPC_running){ /* for XMP/C */

    count = 1;
    blocklength = type_size;
    stride = ainfo[ndims-1].alloc_size * type_size;

    for (int i = 1; i <= target_dim; i++){
      count *= ainfo[i-1].alloc_size;
    }

    for (int i = ndims - 2; i >= target_dim; i--){
      blocklength *= ainfo[i+1].alloc_size;
      stride *= ainfo[i].alloc_size;
    }

  }
  else {
    _XMP_fatal("cannot determin the base language.");
  }

  //
  // calculate base address
  //

  // for lower reflect

  if (lwidth){

    lo_send_array = array_addr;
    lo_recv_array = array_addr;

    for (int i = 0; i < ndims; i++) {

      int lb_send, lb_recv;
      unsigned long long dim_acc;

      if (i == target_dim) {
	lb_send = ainfo[i].local_upper - lwidth + 1;
	lb_recv = ainfo[i].shadow_size_lo - lwidth;;
      }
      else {
	// Note: including shadow area
	lb_send = 0;
	lb_recv = 0;
      }

      dim_acc = ainfo[i].dim_acc;

      lo_send_array = (void *)((char *)lo_send_array + lb_send * dim_acc * type_size);
      lo_recv_array = (void *)((char *)lo_recv_array + lb_recv * dim_acc * type_size);

    }

  }

  // for upper reflect

  if (uwidth){

    hi_send_array = array_addr;
    hi_recv_array = array_addr;

    for (int i = 0; i < ndims; i++) {

      int lb_send, lb_recv;
      unsigned long long dim_acc;

      if (i == target_dim) {
	lb_send = ainfo[i].local_lower;
	lb_recv = ainfo[i].local_upper + 1;
      }
      else {
	// Note: including shadow area
	lb_send = 0;
	lb_recv = 0;
      }

      dim_acc = ainfo[i].dim_acc;

      hi_send_array = (void *)((char *)hi_send_array + lb_send * dim_acc * type_size);
      hi_recv_array = (void *)((char *)hi_recv_array + lb_recv * dim_acc * type_size);

    }

  }

  //
  // Allocate buffers
  //

  if ((_XMPF_running && target_dim != ndims - 1) ||
      (_XMPC_running && target_dim != 0)){
    _XMP_free(reflect->lo_send_buf);
    _XMP_free(reflect->lo_recv_buf);
    _XMP_free(reflect->hi_send_buf);
    _XMP_free(reflect->hi_recv_buf);
  }

  // for lower reflect

  if (lwidth){

    lo_buf_size = lwidth * blocklength * count;

    if ((_XMPF_running && target_dim == ndims - 1) ||
	(_XMPC_running && target_dim == 0)){
      lo_send_buf = lo_send_array;
      lo_recv_buf = lo_recv_array;
    }
    else {
      _XMP_TSTART(t0);
      lo_send_buf = _XMP_alloc(lo_buf_size);
      lo_recv_buf = _XMP_alloc(lo_buf_size);
      _XMP_TEND2(xmptiming_.t_mem, xmptiming_.tdim_mem[target_dim], t0);
    }

  }

  // for upper reflect

  if (uwidth){

    hi_buf_size = uwidth * blocklength * count;

    if ((_XMPF_running && target_dim == ndims - 1) ||
	(_XMPC_running && target_dim == 0)){
      hi_send_buf = hi_send_array;
      hi_recv_buf = hi_recv_array;
    }
    else {
      _XMP_TSTART(t0);
      hi_send_buf = _XMP_alloc(hi_buf_size);
      hi_recv_buf = _XMP_alloc(hi_buf_size);
      _XMP_TEND2(xmptiming_.t_mem, xmptiming_.tdim_mem[target_dim], t0);
    }

  }

  //
  // initialize communication
  //

  int src, dst;

  if (!is_periodic && my_pos == lb_pos){ // no periodic
    lo_rank = MPI_PROC_NULL;
  }

  if (!is_periodic && my_pos == ub_pos){ // no periodic
    hi_rank = MPI_PROC_NULL;
  }

  // for lower shadow

  if (lwidth){
    src = lo_rank;
    dst = hi_rank;
  }
  else {
    src = MPI_PROC_NULL;
    dst = MPI_PROC_NULL;
  }

  if (reflect->req[0] != MPI_REQUEST_NULL){
    MPI_Request_free(&reflect->req[0]);
  }
	
  if (reflect->req[1] != MPI_REQUEST_NULL){
    MPI_Request_free(&reflect->req[1]);
  }

  MPI_Recv_init(lo_recv_buf, lo_buf_size, MPI_BYTE, src,
		_XMP_N_MPI_TAG_REFLECT_LO, *comm, &reflect->req[0]);
  MPI_Send_init(lo_send_buf, lo_buf_size, MPI_BYTE, dst,
		_XMP_N_MPI_TAG_REFLECT_LO, *comm, &reflect->req[1]);

  // for upper shadow

  if (uwidth){
    src = hi_rank;
    dst = lo_rank;
  }
  else {
    src = MPI_PROC_NULL;
    dst = MPI_PROC_NULL;
  }

  if (reflect->req[2] != MPI_REQUEST_NULL){
    MPI_Request_free(&reflect->req[2]);
  }
	
  if (reflect->req[3] != MPI_REQUEST_NULL){
    MPI_Request_free(&reflect->req[3]);
  }

  MPI_Recv_init(hi_recv_buf, hi_buf_size, MPI_BYTE, src,
		_XMP_N_MPI_TAG_REFLECT_HI, *comm, &reflect->req[2]);
  MPI_Send_init(hi_send_buf, hi_buf_size, MPI_BYTE, dst,
		_XMP_N_MPI_TAG_REFLECT_HI, *comm, &reflect->req[3]);

  //
  // cache schedule
  //

  reflect->count = count;
  reflect->blocklength = blocklength;
  reflect->stride = stride;

  reflect->lo_send_array = lo_send_array;
  reflect->lo_recv_array = lo_recv_array;
  reflect->hi_send_array = hi_send_array;
  reflect->hi_recv_array = hi_recv_array;

  reflect->lo_send_buf = lo_send_buf;
  reflect->lo_recv_buf = lo_recv_buf;
  reflect->hi_send_buf = hi_send_buf;
  reflect->hi_recv_buf = hi_recv_buf;

}


static void _XMP_reflect_start(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic,
			       int dummy)
{

  if (_xmp_reflect_pack_flag){
    _XMP_TSTART(t0);
    _XMP_reflect_pack(a, lwidth, uwidth, is_periodic);
    _XMP_TEND(xmptiming_.t_copy, t0);
  }

  for (int i = 0; i < a->dim; i++){

    if (!lwidth[i] && !uwidth[i]) continue;

    _XMP_array_info_t *ai = &(a->info[i]);

    if (ai->shadow_type == _XMP_N_SHADOW_NORMAL){

      _XMP_reflect_sched_t *reflect = ai->reflect_sched;

      _XMP_TSTART(t0);
      MPI_Startall(4, reflect->req);
      _XMP_TEND2(xmptiming_.t_comm, xmptiming_.tdim_comm[i], t0);
    }

  }

}


static void _XMP_reflect_wait(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic)
{
  for (int i = 0; i < a->dim; i++){

    if (!lwidth[i] && !uwidth[i]) continue;

    _XMP_array_info_t *ai = &(a->info[i]);

    if (ai->shadow_type == _XMP_N_SHADOW_NORMAL){

      _XMP_reflect_sched_t *reflect = ai->reflect_sched;

      _XMP_TSTART(t0);
      MPI_Waitall(4, reflect->req, MPI_STATUSES_IGNORE);
      _XMP_TEND2(xmptiming_.t_wait, xmptiming_.tdim_wait[i], t0);
    }
    else if (ai->shadow_type == _XMP_N_SHADOW_FULL){
      _XMP_reflect_shadow_FULL(a->array_addr_p, a, i);
    }

  }

  if (_xmp_reflect_pack_flag){
    _XMP_TSTART(t0);
    _XMP_reflect_unpack(a, lwidth, uwidth, is_periodic);
    _XMP_TEND(xmptiming_.t_copy, t0);
  }

}


void _XMP_reflect_async__(_XMP_array_t *a, int async_id)
{

  int nreqs = 0;
  MPI_Request *reqs;

  if (!a->is_allocated){
    _xmpf_set_reflect_flag = 0;
    return;
  }

  if (!_xmpf_set_reflect_flag){
    for (int i = 0; i < a->dim; i++){
      _XMP_array_info_t *ai = &(a->info[i]);
      _xmp_lwidth[i] = ai->shadow_size_lo;
      _xmp_uwidth[i] = ai->shadow_size_hi;
      _xmp_is_periodic[i] = 0;
    }
  }
    
  _XMP_reflect_sched(a, _xmp_lwidth, _xmp_uwidth, _xmp_is_periodic, 1);

  //
  // Start Comm.
  //

  _XMP_async_comm_t *async = _XMP_get_or_create_async(async_id);

  reqs = &async->reqs[async->nreqs];

  for (int i = 0; i < a->dim; i++){
    _XMP_reflect_sched_t *reflect = a->info[i].reflect_sched;
    if (_xmp_lwidth[i] || _xmp_uwidth[i]){
      if (async->nreqs + nreqs + 4 > _XMP_MAX_ASYNC_REQS){
	_XMP_fatal("too many arrays in an asynchronous reflect");
      }
      memcpy(&reqs[nreqs], reflect->req, 4 * sizeof(MPI_Request));
      nreqs += 4;
    }
  }

  async->nreqs += nreqs;

  _XMP_TSTART(t0);
  MPI_Startall(nreqs, reqs);
  _XMP_TEND(xmptiming_.t_start, t0);

  _xmpf_set_reflect_flag = 0;

}


#else

//
// Reflect with RDMA
//

static void _XMP_reflect_rdma_sched_dim(_XMP_array_t *adesc, int target_dim,
					int lwidth, int uwidth, int is_periodic){

  if (lwidth == 0 && uwidth == 0) return;

  _XMP_array_info_t *ai = &(adesc->info[target_dim]);
  _XMP_array_info_t *ainfo = adesc->info;
  _XMP_ASSERT(ai->align_manner == _XMP_N_ALIGN_BLOCK);
  _XMP_ASSERT(ai->is_shadow_comm_member);

  if (lwidth > ai->shadow_size_lo || uwidth > ai->shadow_size_hi){
    _XMP_fatal("reflect width is larger than shadow width.");
  }

  _XMP_reflect_sched_t *reflect = ai->reflect_sched;

  int target_tdim = ai->align_template_index;
  _XMP_nodes_info_t *ni = adesc->align_template->chunk[target_tdim].onto_nodes_info;

  int ndims = adesc->dim;

  // 0-origin
  int my_pos = ni->rank;
  int lb_pos = _XMP_get_owner_pos(adesc, target_dim, ai->ser_lower);
  int ub_pos = _XMP_get_owner_pos(adesc, target_dim, ai->ser_upper);

  int my_rank = adesc->align_template->onto_nodes->comm_rank;

  int lo_pos = (my_pos == lb_pos) ? ub_pos : my_pos - 1;
  int hi_pos = (my_pos == ub_pos) ? lb_pos : my_pos + 1;

  int lo_rank = my_rank + (lo_pos - my_pos) * ni->multiplier;
  int hi_rank = my_rank + (hi_pos - my_pos) * ni->multiplier;

  int type_size = adesc->type_size;

  void *lo_send_array, *lo_recv_array;
  void *hi_send_array, *hi_recv_array;

  uint64_t rdma_raddr;

  //
  // calculate offset
  //

  int count = 0, blocklength = 0;
  long long stride = 0;

  if (_XMPF_running && !_XMPC_running){ /* for XMP/F */

    count = 1;
    blocklength = type_size;
    stride = ainfo[0].alloc_size * type_size;

    for (int i = ndims - 2; i >= target_dim; i--){
      count *= ainfo[i+1].alloc_size;
    }

    for (int i = 1; i <= target_dim; i++){
      blocklength *= ainfo[i-1].alloc_size;
      stride *= ainfo[i].alloc_size;
    }

  }
  else if (!_XMPF_running && _XMPC_running){ /* for XMP/C */

    count = 1;
    blocklength = type_size;
    stride = ainfo[ndims-1].alloc_size * type_size;

    for (int i = 1; i <= target_dim; i++){
      count *= ainfo[i-1].alloc_size;
    }

    for (int i = ndims - 2; i >= target_dim; i--){
      blocklength *= ainfo[i+1].alloc_size;
      stride *= ainfo[i].alloc_size;
    }

  }
  else {
    _XMP_fatal("cannot determin the base language.");
  }
  
  //
  // calculate base address
  //

  // for lower reflect
    
  while ((rdma_raddr = FJMPI_Rdma_get_remote_addr(hi_rank, adesc->rdma_memid)) == FJMPI_RDMA_ERROR);

  if (lwidth){
    
    lo_send_array = (void *)adesc->rdma_addr;
    lo_recv_array = (void *)rdma_raddr;

    for (int i = 0; i < ndims; i++) {
      
      int lb_send, lb_recv, dim_acc;
      
      if (i == target_dim) {
	lb_send = ainfo[i].local_upper - lwidth + 1;
	lb_recv = ainfo[i].shadow_size_lo - lwidth;
      }
      else {
	// Note: including shadow area
	lb_send = 0;
	lb_recv = 0;
      }
      
      dim_acc = ainfo[i].dim_acc;
      
      lo_send_array = (void *)((uint64_t)lo_send_array + lb_send * dim_acc * type_size);
      lo_recv_array = (void *)((uint64_t)lo_recv_array + lb_recv * dim_acc * type_size);
      
    }
    
  }
  
  // for upper reflect
  
  while ((rdma_raddr = FJMPI_Rdma_get_remote_addr(lo_rank, adesc->rdma_memid)) == FJMPI_RDMA_ERROR);

  if (uwidth){
    
    hi_send_array = (void *)adesc->rdma_addr;
    hi_recv_array = (void *)rdma_raddr;

    for (int i = 0; i < ndims; i++) {
      
      int lb_send, lb_recv, dim_acc;
      
      if (i == target_dim) {
	lb_send = ainfo[i].local_lower;
	lb_recv = ainfo[i].local_upper + 1;
      }
      else {
	// Note: including shadow area
	lb_send = 0;
	lb_recv = 0;
      }
      
      dim_acc = ainfo[i].dim_acc;
	
      hi_send_array = (void *)((uint64_t)hi_send_array + lb_send * dim_acc * type_size);
      hi_recv_array = (void *)((uint64_t)hi_recv_array + lb_recv * dim_acc * type_size);
      
    }
      
  }
  
  //
  // cache schedule
  //

  if (!is_periodic && my_pos == lb_pos){ // no periodic
    lo_rank = -1;
  }

  if (!is_periodic && my_pos == ub_pos){ // no periodic
    hi_rank = -1;
  }

  reflect->count = count;
  reflect->blocklength = blocklength;
  reflect->stride = stride;

  reflect->lo_send_array = lo_send_array;
  reflect->lo_recv_array = lo_recv_array;
  reflect->hi_send_array = hi_send_array;
  reflect->hi_recv_array = hi_recv_array;

  reflect->lo_rank = lo_rank;
  reflect->hi_rank = hi_rank;

}


/* static void _XMP_reflect_sync(_XMP_array_t **a_desc, int *lwidth, int *uwidth, int *is_periodic, */
/* 			       int tag) */
/* { */
/*   _XMP_array_t *a = *a_desc; */

/*   xmpf_dbg_printf("_XMP_reflect_sync starts\n"); */

/*   int nrdmas0 = 0, nrdmas1 = 0, nrdmas2 = 0, nrdmas3 = 0; */

/*   for (int i = 0; i < a->dim; i++){ */

/*     _XMP_reflect_sched_t *reflect = a->info[i].reflect_sched; */

/*     // for lower reflect */

/*     if (lwidth[i] && reflect->hi_rank != -1){ */

/*       int hi_rank = reflect->hi_rank; */

/*       uint64_t lo_recv_array = (uint64_t)reflect->lo_recv_array; */
/*       uint64_t lo_send_array = (uint64_t)reflect->lo_send_array; */

/*       FJMPI_Rdma_put(hi_rank, tag, */
/* 		     lo_recv_array, lo_send_array, */
/* 		     0, */
/* 		     FJMPI_RDMA_LOCAL_NIC0 | FJMPI_RDMA_REMOTE_NIC2 | FJMPI_RDMA_REMOTE_NOTICE); */

/*       nrdmas0++; */
/*     } */

/*     if (lwidth[i] && reflect->lo_rank != -1) nrdmas2++; */

/*     // for upper reflect */

/*     if (uwidth[i] && reflect->lo_rank != -1){ */

/*       int lo_rank = reflect->lo_rank; */

/*       uint64_t hi_recv_array = (uint64_t)reflect->hi_recv_array; */
/*       uint64_t hi_send_array = (uint64_t)reflect->hi_send_array; */

/*       FJMPI_Rdma_put(lo_rank, tag, */
/* 		     hi_recv_array, hi_send_array, */
/* 		     0, */
/* 		     FJMPI_RDMA_LOCAL_NIC1 | FJMPI_RDMA_REMOTE_NIC3 | FJMPI_RDMA_REMOTE_NOTICE); */

/*       nrdmas1++; */
/*     } */

/*     if (uwidth[i] && reflect->hi_rank != -1) nrdmas3++; */

/*   } */

/*   // flush send completion */
/*   while (nrdmas0 || nrdmas1){ */
/*     xmpf_dbg_printf("nrdmas0 = %d, nrdmas1 = %d, nrdmas2 = %d, nrdmas3 = %d\n", */
/* 		    nrdmas0, nrdmas1, nrdmas2, nrdmas3); */
/*     while (FJMPI_Rdma_poll_cq(FJMPI_RDMA_NIC0, NULL) == FJMPI_RDMA_NOTICE){ */
/*       nrdmas0--; */
/*     } */
/*     while (FJMPI_Rdma_poll_cq(FJMPI_RDMA_NIC1, NULL) == FJMPI_RDMA_NOTICE){ */
/*       nrdmas1--; */
/*     } */
/*   } */

/*   // check receive completion */
/*   while (nrdmas2 || nrdmas3){ */
/*     xmpf_dbg_printf("nrdmas0 = %d, nrdmas1 = %d, nrdmas2 = %d, nrdmas3 = %d\n", */
/* 		    nrdmas0, nrdmas1, nrdmas2, nrdmas3); */
/*     while (FJMPI_Rdma_poll_cq(FJMPI_RDMA_NIC2, NULL) == FJMPI_RDMA_HALFWAY_NOTICE){ */
/*       nrdmas2--; */
/*     } */
/*     while (FJMPI_Rdma_poll_cq(FJMPI_RDMA_NIC3, NULL) == FJMPI_RDMA_HALFWAY_NOTICE){ */
/*       nrdmas3--; */
/*     } */
/*   } */

/*   xmpf_dbg_printf("_XMP_reflect_sync ends\n"); */

/* } */


static void _XMP_reflect_start(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic,
			       int tag)
{
  _XMP_TSTART(t1);

  xmp_barrier();

  for (int i = 0; i < a->dim; i++){

    _XMP_reflect_sched_t *reflect = a->info[i].reflect_sched;

    _XMP_TSTART(t0);

    // for lower reflect

    if (lwidth[i] && reflect->hi_rank != -1){
      for (int j = 0; j < reflect->count; j++){
	FJMPI_Rdma_put(reflect->hi_rank, tag,
		       (uint64_t)reflect->lo_recv_array + j * reflect->stride,
		       (uint64_t)reflect->lo_send_array + j * reflect->stride,
		       lwidth[i] * reflect->blocklength,
		       FJMPI_RDMA_LOCAL_NIC0 | FJMPI_RDMA_REMOTE_NIC2);
      }
    }

    // for upper reflect

    if (uwidth[i] && reflect->lo_rank != -1){
      for (int j = 0; j < reflect->count; j++){
	FJMPI_Rdma_put(reflect->lo_rank, tag,
		       (uint64_t)reflect->hi_recv_array + j * reflect->stride,
		       (uint64_t)reflect->hi_send_array + j * reflect->stride,
		       uwidth[i] * reflect->blocklength,
		       FJMPI_RDMA_LOCAL_NIC1 | FJMPI_RDMA_REMOTE_NIC3);
      }
    }

    _XMP_TEND(xmptiming_.tdim_comm[i], t0);

  }

  _XMP_TEND(xmptiming_.t_comm, t1);

}


static void _XMP_reflect_wait(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic)
{

  int nrdmas0 = 0, nrdmas1 = 0;

  _XMP_TSTART(t0);

  for (int i = 0; i < a->dim; i++){
    _XMP_reflect_sched_t *reflect = a->info[i].reflect_sched;
    if (lwidth[i] && reflect->hi_rank != -1) nrdmas0 += reflect->count;
    if (uwidth[i] && reflect->lo_rank != -1) nrdmas1 += reflect->count;
  }

  while (nrdmas0 || nrdmas1){
    while (FJMPI_Rdma_poll_cq(FJMPI_RDMA_NIC0, NULL) == FJMPI_RDMA_NOTICE){
      nrdmas0--;
    }
    while (FJMPI_Rdma_poll_cq(FJMPI_RDMA_NIC1, NULL) == FJMPI_RDMA_NOTICE){
      nrdmas1--;
    }
  }

  xmp_barrier();

  _XMP_TEND(xmptiming_.t_wait, t0);

}


void _XMP_reflect_async__(_XMP_array_t *a, int async_id)
{

  if (!a->is_allocated){
    _xmpf_set_reflect_flag = 0;
    return;
  }

  if (!_xmpf_set_reflect_flag){
    for (int i = 0; i < a->dim; i++){
      _XMP_array_info_t *ai = &(a->info[i]);
      _xmp_lwidth[i] = ai->shadow_size_lo;
      _xmp_uwidth[i] = ai->shadow_size_hi;
      _xmp_is_periodic[i] = 0;
    }
  }
    
  _XMP_reflect_sched(a, _xmp_lwidth, _xmp_uwidth, _xmp_is_periodic, 1);
  _XMP_reflect_start(a, _xmp_lwidth, _xmp_uwidth, _xmp_is_periodic, async_id);

  _XMP_async_comm_t *async = _XMP_get_or_create_async(async_id);
  _XMP_free(async->reqs); async->reqs = NULL; // reqs not needed in RDMA reflects.

  for (int i = 0; i < a->dim; i++){
    _XMP_reflect_sched_t *reflect = a->info[i].reflect_sched;
    if (_xmp_lwidth[i] && reflect->hi_rank != -1) async->nreqs += reflect->count;
    if (_xmp_uwidth[i] && reflect->lo_rank != -1) async->nreqs += reflect->count;
  }

  _xmpf_set_reflect_flag = 0;

}

#endif


/* int _XMP_get_owner_pos_BLOCK(_XMP_array_t *a, int dim, int index){ */

/*   _XMP_ASSERT(a->info[dim].align_manner == _XMP_N_ALIGN_BLOCK); */

/*   int align_offset = a->info[dim].align_subscript; */

/*   int tdim = a->info[dim].align_template_index; */
/*   int tlb = a->align_template->info[tdim].ser_lower; */
/*   int chunk = a->align_template->chunk[tdim].par_chunk_width; */

/*   int pos = (index + align_offset - tlb) / chunk; */

/*   return pos; */
/* } */


int _XMP_get_owner_pos(_XMP_array_t *a, int dim, int index){

  int align_offset = a->info[dim].align_subscript;

  int tdim = a->info[dim].align_template_index;
  int tlb = a->align_template->info[tdim].ser_lower;
  int chunk = a->align_template->chunk[tdim].par_chunk_width;

  int pos;
  switch (a->info[dim].align_manner){

  case _XMP_N_ALIGN_BLOCK:
    pos = (index + align_offset - tlb) / chunk;
    return pos;

  case _XMP_N_ALIGN_GBLOCK:
    {
      int tpos = index + align_offset; // tlb is not subtracted because the mapping_array is 1-origin.
      long long *m = a->align_template->chunk[tdim].mapping_array;
      int np = a->align_template->chunk[tdim].onto_nodes_info->size;
      for (int i = 0; i < np; i++){
	if (m[i] <= tpos && tpos < m[i+1]){
	  return i;
	}
      }
    }
  }

  _XMP_fatal("cannot calculate position");
  return -1;
}


void _XMP_reflect_pack(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic)
{

  int lb = 0, ub = 0;

  if (_XMPF_running && (!_XMPC_running)){ /* for XMP/F */
    lb = 0;
    ub = a->dim - 1;
  }
  else if ((!_XMPF_running) && _XMPC_running){ /* for XMP/C */
    lb = 1;
    ub = a->dim;
  }
  else {
    _XMP_fatal("cannot determin the base language.");
  }

  for (int i = lb; i < ub; i++){

    _XMP_array_info_t *ai = &(a->info[i]);
    _XMP_reflect_sched_t *reflect = ai->reflect_sched;

    if (ai->shadow_type == _XMP_N_SHADOW_NORMAL){

      // for lower reflect
      if (lwidth[i]){
	_XMP_pack_vector((char *)reflect->lo_send_buf,
			 (char *)reflect->lo_send_array,
			 reflect->count, lwidth[i] * reflect->blocklength,
			 reflect->stride);
      }

      // for upper reflect
      if (uwidth[i]){
	_XMP_pack_vector((char *)reflect->hi_send_buf,
			 (char *)reflect->hi_send_array,
			 reflect->count, uwidth[i] * reflect->blocklength,
			 reflect->stride);
      }

    }

  }

}


void _XMP_reflect_unpack(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic)
{

  int lb = 0, ub = 0;

  if (_XMPF_running && (!_XMPC_running)){ /* for XMP/F */
    lb = 0;
    ub = a->dim - 1;
  }
  else if ((!_XMPF_running) && _XMPC_running){ /* for XMP/C */
    lb = 1;
    ub = a->dim;
  }
  else {
    _XMP_fatal("cannot determin the base language.");
  }

  for (int i = lb; i < ub; i++){

    _XMP_array_info_t *ai = &(a->info[i]);
    _XMP_reflect_sched_t *reflect = ai->reflect_sched;

    if (ai->shadow_type == _XMP_N_SHADOW_NORMAL){

      // for lower reflect
      if (lwidth[i]){
	_XMP_unpack_vector((char *)reflect->lo_recv_array,
			   (char *)reflect->lo_recv_buf,
			   reflect->count, lwidth[i] * reflect->blocklength,
			   reflect->stride);
      }

      // for upper reflect
      if (uwidth[i]){
	_XMP_unpack_vector((char *)reflect->hi_recv_array,
			   (char *)reflect->hi_recv_buf,
			   reflect->count, uwidth[i] * reflect->blocklength,
			   reflect->stride);
      }

    }

  }

}


static void _XMP_reflect_dir(_XMP_array_t *adesc, int ishadow[],
			     int lwidth[], int uwidth[], int is_periodic_dim[]){

  //if (lwidth == 0 && uwidth == 0) return;

  /* _XMP_array_info_t *ai = &(adesc->info[target_dim]); */
  /* _XMP_array_info_t *ainfo = adesc->info; */
  /* _XMP_ASSERT(ai->align_manner == _XMP_N_ALIGN_BLOCK); */
  /* _XMP_ASSERT(ai->is_shadow_comm_member); */

  /* if (lwidth > ai->shadow_size_lo || uwidth > ai->shadow_size_hi){ */
  /*   _XMP_fatal("reflect width is larger than shadow width."); */
  /* } */

  /* _XMP_reflect_sched_t *reflect = ai->reflect_sched; */

  /* int target_tdim = ai->align_template_index; */
  /* _XMP_nodes_info_t *ni = adesc->align_template->chunk[target_tdim].onto_nodes_info; */

  int ndims = adesc->dim;

  _XMP_array_info_t *ainfo = adesc->info;

  MPI_Comm *comm = adesc->align_template->onto_nodes->comm;
  int my_rank = adesc->align_template->onto_nodes->comm_rank;

  /* if (my_rank == 0) printf("----------------------------------------\n"); */
  /* fflush(stdout); */
  /* xmp_barrier(); */

  /* int lo_rank = my_rank; */
  /* int hi_rank = my_rank; */

  int src = my_rank;
  int dst = my_rank;

  MPI_Datatype send_dtype, recv_dtype;
  MPI_Request send_req, recv_req;

  int type_size = adesc->type_size;

  int width[_XMP_N_MAX_DIM] = { 0 };

  int is_periodic = 1;
  int at_tail = 0, at_head = 0;

  void *recv_buf = adesc->array_addr_p;
  void *send_buf = adesc->array_addr_p;

  //
  // setup neighbor nodes
  //

  for (int i = 0; i < ndims; i++){

    if (ishadow[i] == 0) continue;

    width[i] = ishadow[i] > 0 ? uwidth[i] : lwidth[i];
    is_periodic = is_periodic * is_periodic_dim[i];

    _XMP_array_info_t *ai = &(adesc->info[i]);

    _XMP_ASSERT(ai->align_manner == _XMP_N_ALIGN_BLOCK);
    _XMP_ASSERT(ai->is_shadow_comm_member);

    if (lwidth[i] > ai->shadow_size_lo || uwidth[i] > ai->shadow_size_hi){
      _XMP_fatal("reflect width is larger than shadow width.");
    }

    //_XMP_reflect_sched_t *reflect = ai->reflect_sched;

    int tdim = ai->align_template_index;
    _XMP_nodes_info_t *ni = adesc->align_template->chunk[tdim].onto_nodes_info;
    
    // 0-origin
    int my_pos = ni->rank;
    int lb_pos = _XMP_get_owner_pos(adesc, i, ai->ser_lower);
    int ub_pos = _XMP_get_owner_pos(adesc, i, ai->ser_upper);

    int src_pos;
    int dst_pos;

    if (ishadow[i] > 0){
      src_pos = my_pos + 1;
      dst_pos = my_pos - 1;
      if (my_pos == lb_pos){
	at_head = 1;
	dst_pos = ub_pos;
      }
      if (my_pos == ub_pos){
	at_tail = 1;
	src_pos = lb_pos;
      }
    }
    else { //ishadow[i] < 0
      src_pos = my_pos - 1;
      dst_pos = my_pos + 1;
      if (my_pos == lb_pos){
	at_tail = 1;
	src_pos = ub_pos;
      }
      if (my_pos == ub_pos){
	at_head = 1;
	dst_pos = lb_pos;
      }
    }

    src = src + (src_pos - my_pos) * ni->multiplier;
    dst = dst + (dst_pos - my_pos) * ni->multiplier;

  }

  src = (is_periodic || !at_tail) ? src : MPI_PROC_NULL;
  dst = (is_periodic || !at_head) ? dst : MPI_PROC_NULL;

  /* fflush(stdout); */
  /* xmp_barrier(); */

  /* printf("(%+d", ishadow[0]); */
  /* for (int i = 1; i < ndims; i++){ */
  /*   printf(", %+d", ishadow[i]); */
  /* } */
  /* printf(") "); */

  /* printf("(%d, %d, %d) ", is_periodic, at_tail, at_head); */

  /* if (src == MPI_PROC_NULL && dst == MPI_PROC_NULL){ */
  /*   printf("[%2d]", my_rank); */
  /* } */
  /* else { */
  /*   if (src != MPI_PROC_NULL) printf("%2d -> [%2d]", src, my_rank); */
  /*   if (src != MPI_PROC_NULL && dst != MPI_PROC_NULL) printf(", "); */
  /*   if (dst != MPI_PROC_NULL) printf("[%2d] -> %2d", my_rank, dst); */
  /* } */
  /* printf("\n"); */
  
  //
  // setup MPI_data_type
  //

  /* int count = 1; */
  /* /\* int blocklength = 1; *\/ */
  /* /\* long long stride = 1; *\/ */
  /* int blocklength = type_size; */
  /* long long stride = type_size; */

  /* /\* for (int i = 1; i <= target_dim; i++){ *\/ */
  /* /\* 	count *= ainfo[i-1].alloc_size; *\/ */
  /* /\* } *\/ */

  /* for (int i = 0; i < ndims; i++){ */

  /*   if (ishadow[i] == 0){ */
  /*     count *= ainfo[i].par_size; */
  /*   } */
  /*   else { */
  /*     count *= width[i]; */
  /*   } */

  /*   if (ainfo[i].shadow_size_lo != 0 || ainfo[i].shadow_size_hi != 0) break; */
  /* } */

  /* /\* for (int i = ndims - 2; i >= target_dim; i--){ *\/ */
  /* /\* 	blocklength *= ainfo[i+1].alloc_size; *\/ */
  /* /\* 	stride *= ainfo[i].alloc_size; *\/ */
  /* /\* } *\/ */

  /* for (int i = ndims - 1; i >= 0; i--){ */

  /*   if (ishadow[i] == 0){ */
  /*     blocklength *= ainfo[i].par_size; */
  /*   } */
  /*   else { */
  /*     blocklength *= width[i]; */
  /*   } */

  /*   stride *= ainfo[i].alloc_size; */

  /*   if (ainfo[i].shadow_size_lo != 0 || ainfo[i].shadow_size_hi != 0) break; */

  /* } */

  int sizes[_XMP_N_MAX_DIM];
  int subsizes[_XMP_N_MAX_DIM];
  int send_starts[_XMP_N_MAX_DIM];
  int recv_starts[_XMP_N_MAX_DIM];

  for (int i = 0; i < ndims; i++){

    sizes[i] = ainfo[i].alloc_size;
    subsizes[i] = (ishadow[i] == 0) ? ainfo[i].par_size : width[i];

    if (ishadow[i] == 0){
      // excludes shadow area
      send_starts[i] = ainfo[i].shadow_size_lo;
      recv_starts[i] = ainfo[i].shadow_size_lo;
    }
    else if (ishadow[i] > 0){
      send_starts[i] = ainfo[i].shadow_size_lo;
      recv_starts[i] = ainfo[i].local_upper + 1;
    }
    else {
      send_starts[i] = ainfo[i].local_upper - width[i] + 1;
      recv_starts[i] = ainfo[i].shadow_size_lo - width[i];
    }

  }

  MPI_Type_create_subarray(ndims, sizes, subsizes, send_starts,
			   MPI_ORDER_C, MPI_INT, &send_dtype);
  MPI_Type_create_subarray(ndims, sizes, subsizes, recv_starts,
			   MPI_ORDER_C, MPI_INT, &recv_dtype);

  /* if (my_rank == 0){ */

  /*   printf("(%+d", ishadow[0]); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %+d", ishadow[i]); */
  /*   } */
  /*   printf(")\n"); */

  /*   printf(" sizes = (%d", sizes[0]/type_size); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %d", sizes[i]/type_size); */
  /*   } */
  /*   printf(")\n"); */

  /*   printf(" subsizes = (%d", subsizes[0]/type_size); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %d", subsizes[i]/type_size); */
  /*   } */
  /*   printf(")\n"); */

  /*   printf(" send_starts = (%d", send_starts[0]/type_size); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %d", send_starts[i]/type_size); */
  /*   } */
  /*   printf(")\n"); */

  /*   printf(" recv_starts = (%d", recv_starts[0]/type_size); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %d", recv_starts[i]/type_size); */
  /*   } */
  /*   printf(")\n"); */
  /* } */

    /* if (_XMPF_running && (!_XMPC_running)){ /\* for XMP/F *\/ */

    /*   count = 1; */
    /*   blocklength = type_size; */
    /*   stride = ainfo[0].alloc_size * type_size; */

    /*   for (int i = ndims - 2; i >= target_dim; i--){ */
    /* 	count *= ainfo[i+1].alloc_size; */
    /*   } */

    /*   for (int i = 1; i <= target_dim; i++){ */
    /* 	blocklength *= ainfo[i-1].alloc_size; */
    /* 	stride *= ainfo[i].alloc_size; */
    /*   } */

    /* } */
    /* else if ((!_XMPF_running) && _XMPC_running){ /\* for XMP/C *\/ */

      /* count = 1; */
      /* blocklength = type_size; */
      /* stride = ainfo[ndims-1].alloc_size * type_size; */

      /* for (int i = 1; i <= target_dim; i++){ */
      /* 	count *= ainfo[i-1].alloc_size; */
      /* } */

      /* for (int i = ndims - 2; i >= target_dim; i--){ */
      /* 	blocklength *= ainfo[i+1].alloc_size; */
      /* 	stride *= ainfo[i].alloc_size; */
      /* } */

    /* } */
    /* else { */
    /*   _XMP_fatal("cannot determine the base language."); */
    /* } */

  /* if (reflect->datatype_lo != MPI_DATATYPE_NULL){ */
  /*   MPI_Type_free(&reflect->datatype_lo); */
  /* } */

  //MPI_Type_vector(count, blocklength, stride, MPI_BYTE, &dtype);
  MPI_Type_commit(&send_dtype);
  MPI_Type_commit(&recv_dtype);

  /* if (my_rank == 0){ */
  /*   printf("count = %d, blocklength = %d, stride = %d at ", */
  /* 	   count, blocklength/type_size, stride/type_size); */
  /*   printf("(%+d", ishadow[0]); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %+d", ishadow[i]); */
  /*   } */
  /*   printf(")\n"); */
  /* } */

  //
  // calculate base address
  //

  /* int s[_XMP_N_MAX_DIM], r[_XMP_N_MAX_DIM]; */

  /* for (int i = 0; i < ndims; i++) { */

  /*   int send, recv; */

  /*   if (ishadow[i] == 0){ */
  /*     // excludes shadow area */
  /*     send = ainfo[i].shadow_size_lo; */
  /*     recv = ainfo[i].shadow_size_lo; */
  /*   } */
  /*   else if (ishadow[i] > 0){ */
  /*     send = ainfo[i].shadow_size_lo; */
  /*     recv = ainfo[i].local_upper + 1; */
  /*   } */
  /*   else { */
  /*     send = ainfo[i].local_upper - width[i] + 1; */
  /*     recv = ainfo[i].shadow_size_lo - width[i]; */
  /*   } */

  /*   int dim_acc = ainfo[i].dim_acc; */

  /*   send_buf = (void *)((char *)send_buf + send * dim_acc * type_size); */
  /*   recv_buf = (void *)((char *)recv_buf + recv * dim_acc * type_size); */

  /*   s[i] = send; */
  /*   r[i] = recv; */

  /* } */

  /* if (my_rank == 0){ */
  /*   printf("send: (%d", s[0]); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %d", s[i]); */
  /*   } */
  /*   printf(")\n"); */
  /*   printf("recv: (%d", r[0]); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %d", r[i]); */
  /*   } */
  /*   printf(") at "); */

  /*   printf("(%+d", ishadow[0]); */
  /*   for (int i = 1; i < ndims; i++){ */
  /*     printf(", %+d", ishadow[i]); */
  /*   } */
  /*   printf(")\n"); */

  /* } */

  //
  // initialize communication
  //

  MPI_Request req[2];

  MPI_Recv_init(recv_buf, 1, recv_dtype, src,
  		_XMP_N_MPI_TAG_REFLECT_LO, *comm, &req[0]);
  MPI_Send_init(send_buf, 1, send_dtype, dst,
  		_XMP_N_MPI_TAG_REFLECT_LO, *comm, &req[1]);

  MPI_Startall(2, req);
  MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

  MPI_Type_free(&send_dtype);
  MPI_Type_free(&recv_dtype);

}


void _XMP_reflect_(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic){

  int n = a->dim;

  int lb[_XMP_N_MAX_DIM] = {0};
  int ub[_XMP_N_MAX_DIM] = {0};

  for (int i = 0; i < n; i++){
    if (lwidth[i] > 0) lb[i] = -1;
    if (uwidth[i] > 0) ub[i] = 1;
  }

  /* lb[0] = 0; ub[0] = 0; */
  /* lb[1] = 0; ub[1] = 0; */
  /*            ub[2] = 0; */

  int ishadow[_XMP_N_MAX_DIM];

  for (ishadow[0] = lb[0]; ishadow[0] <= ub[0]; ishadow[0]++){
  for (ishadow[1] = lb[1]; ishadow[1] <= ub[1]; ishadow[1]++){
  for (ishadow[2] = lb[2]; ishadow[2] <= ub[2]; ishadow[2]++){
  for (ishadow[3] = lb[3]; ishadow[3] <= ub[3]; ishadow[3]++){
  for (ishadow[4] = lb[4]; ishadow[4] <= ub[4]; ishadow[4]++){
  for (ishadow[5] = lb[5]; ishadow[5] <= ub[5]; ishadow[5]++){
  for (ishadow[6] = lb[6]; ishadow[6] <= ub[6]; ishadow[6]++){

    // When ishadow > 0, upper shadow is to be updated, and vice versa.

    int nnzero = 0;
    for (int i = 0; i < n; i++){
      if (ishadow[i] != 0) nnzero++;
    }
    if (nnzero == 0) continue;

    _XMP_reflect_dir(a, ishadow, lwidth, uwidth, is_periodic);

    /* fflush(stdout); */
    /* xmp_barrier(); */

  }
  }
  }
  }
  }
  }
  }

}


// for test

static void _XMP_reflect0_(_XMP_array_t *a, int *lwidth, int *uwidth, int *is_periodic)
{

  for (int i = 0; i < a->dim; i++){

    _XMP_array_info_t *ai = &(a->info[i]);

    if (ai->shadow_type == _XMP_N_SHADOW_NONE){
      continue;
    }
    else if (ai->shadow_type == _XMP_N_SHADOW_NORMAL){

      _XMP_reflect_sched_t *reflect = ai->reflect_sched;

      if (lwidth[i] || uwidth[i]){

	_XMP_ASSERT(reflect);

	if (reflect->is_periodic == -1 /* not set yet */ ||
	    lwidth[i] != reflect->lo_width ||
	    uwidth[i] != reflect->hi_width ||
	    is_periodic[i] != reflect->is_periodic){

	  reflect->lo_width = lwidth[i];
	  reflect->hi_width = uwidth[i];
	  reflect->is_periodic = is_periodic[i];

	  if (_xmp_reflect_pack_flag){
	    _XMP_reflect_pcopy_sched_dim(a, i, lwidth[i], uwidth[i], is_periodic[i]);
	  }
	  else {
	    _XMP_reflect_normal_sched_dim(a, i, lwidth[i], uwidth[i], is_periodic[i]);
	  }

	}

	// pack
	if (_xmp_reflect_pack_flag && i != 0){

	  // for lower reflect
	  if (lwidth[i]){
	    _XMP_pack_vector((char *)reflect->lo_send_buf,
			     (char *)reflect->lo_send_array,
			     reflect->count, lwidth[i] * reflect->blocklength,
			     reflect->stride);
	  }

	  // for upper reflect
	  if (uwidth[i]){
	    _XMP_pack_vector((char *)reflect->hi_send_buf,
			     (char *)reflect->hi_send_array,
			     reflect->count, uwidth[i] * reflect->blocklength,
			     reflect->stride);
	  }
	      
	}

	MPI_Startall(4, reflect->req);
	MPI_Waitall(4, reflect->req, MPI_STATUSES_IGNORE);

	// unpack
	if (_xmp_reflect_pack_flag && i != 0){

	  // for lower reflect
	  if (lwidth[i]){
	    _XMP_unpack_vector((char *)reflect->lo_recv_array,
			       (char *)reflect->lo_recv_buf,
			       reflect->count, lwidth[i] * reflect->blocklength,
			       reflect->stride);
	  }

	  // for upper reflect
	  if (uwidth[i]){
	    _XMP_unpack_vector((char *)reflect->hi_recv_array,
			       (char *)reflect->hi_recv_buf,
			       reflect->count, uwidth[i] * reflect->blocklength,
			       reflect->stride);
	  }

	}

      }

    }
    
  }


}
