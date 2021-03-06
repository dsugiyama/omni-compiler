#include "xmp_internal.h"
#include "tca-api.h"
#include "xmp.h"
#include "cuda_runtime.h"
#define _XMP_TCA_DMAC 0
#define _XMP_TCA_USE_PACK_SIZE 8 // Byte

void _XMP_gpu_pack_vector_async(char * restrict dst, char * restrict src, int count, int blocklength, long stride, size_t typesize, void* async_id);
void _XMP_gpu_unpack_vector_async(char * restrict dst, char * restrict src, int count, int blocklength, long stride, size_t typesize, void* async_id);

#ifdef _XMP_TCA_DEBUG
#define _XMP_TCA_DEBUG(...) printf("%s(%d)[%s]: ", __FILE__, __LINE__, __func__); printf(__VA_ARGS__);
#else
#define _XMP_TCA_DEBUG(...)
#endif

static void _XMP_gpu_wait_async(void *async_id)
{
  cudaStream_t st = *((cudaStream_t*)async_id);
  cudaStreamSynchronize(st);
}

static void create_tca_handle_list(void **tca_handle, void *acc_addr, size_t array_size)
{
  tcaHandle *h, local_handle;

  TCA_CHECK(tcaCreateHandle(&local_handle, acc_addr, array_size, tcaMemoryGPU));

  h = _XMP_alloc(sizeof(tcaHandle) * _XMP_world_size);
  MPI_Allgather(&local_handle, sizeof(tcaHandle), MPI_BYTE,
                h, sizeof(tcaHandle), MPI_BYTE, MPI_COMM_WORLD);

  *tca_handle = h;
}

static void _XMP_create_TCA_handle(void *acc_addr, _XMP_array_t *adesc)
{
  if(adesc->set_handle)
    return;

  size_t size = (size_t)(adesc->type_size * adesc->total_elmts);

  _XMP_TCA_DEBUG("[%d] tcaCreateHandle size = %d addr=%p\n", _XMP_world_rank, size, acc_addr);
  create_tca_handle_list(&(adesc->tca_handle), acc_addr, size);

  adesc->tca_reflect_desc = tcaDescNew();
  adesc->set_handle = _XMP_N_INT_TRUE;
}

static void _XMP_reflect_acc_sched_dim(_XMP_array_t *adesc, void *acc_addr, int dim, int is_periodic)
{
  _XMP_array_info_t *ai = &(adesc->info[dim]);
  _XMP_reflect_sched_t *reflect = ai->reflect_acc_sched;
  _XMP_array_info_t *ainfo = adesc->info;
  int target_tdim = ai->align_template_index;
  _XMP_nodes_info_t *ni = adesc->align_template->chunk[target_tdim].onto_nodes_info;
  int lwidth = ai->shadow_size_lo;
  int uwidth = ai->shadow_size_hi;

  int my_pos = ni->rank;
  int lb_pos = _XMP_get_owner_pos(adesc, dim, ai->ser_lower);
  int ub_pos = _XMP_get_owner_pos(adesc, dim, ai->ser_upper);
  int lo_pos = (my_pos == lb_pos) ? ub_pos : my_pos - 1;
  int hi_pos = (my_pos == ub_pos) ? lb_pos : my_pos + 1;
  int my_rank = adesc->align_template->onto_nodes->comm_rank;
  int lo_rank = my_rank + (lo_pos - my_pos) * ni->multiplier;
  int hi_rank = my_rank + (hi_pos - my_pos) * ni->multiplier;

  int count, blocklength;
  int type_size = adesc->type_size;
  int ndims = adesc->dim;
  long long stride;

  void *lo_send_device_buf = NULL;
  void *lo_recv_device_buf = NULL;
  void *hi_send_device_buf = NULL;
  void *hi_recv_device_buf = NULL;

  void *lo_send_handle = NULL;
  void *lo_recv_handle = NULL;
  void *hi_send_handle = NULL;
  void *hi_recv_handle = NULL;

  reflect->lo_async_id = NULL;
  reflect->hi_async_id = NULL;

  if (_XMPF_running && !_XMPC_running){ /* for XMP/F */
    count = 1;
    blocklength = type_size;
    stride = ainfo[0].alloc_size * type_size;

    for(int i=ndims-2; i>=dim; i--)
      count *= ainfo[i+1].alloc_size;

    for(int i=1; i<=dim; i++){
      blocklength *= ainfo[i-1].alloc_size;
      stride *= ainfo[i].alloc_size;
    }
  }
  else if(!_XMPF_running && _XMPC_running){ /* for XMP/C */
    count = 1;
    blocklength = type_size;
    stride = ainfo[ndims-1].alloc_size * type_size;

    for(int i=1; i <= dim; i++)
      count *= ainfo[i-1].alloc_size;

    for(int i=ndims-2; i >= dim; i--){
      blocklength *= ainfo[i+1].alloc_size;
      stride *= ainfo[i].alloc_size;
    }
  }
  else{
    _XMP_fatal("cannot determin the base language.");
  }

  if (!is_periodic && my_pos == lb_pos) // no periodic
    lo_rank = -1;

  if (!is_periodic && my_pos == ub_pos) // no periodic
    hi_rank = -1;

  // Calulate offset
  off_t hi_src_offset = 0, hi_dst_offset = 0;
  if(lwidth){
    for(int i=0; i<ndims; i++) {
      int lb_send, lb_recv;
      unsigned long long dim_acc;

      if (i == dim){
        lb_send = ainfo[i].local_upper - lwidth + 1;
        lb_recv = ainfo[i].shadow_size_lo - lwidth;
      }
      else {
        // Note: including shadow area
        lb_send = 0;
        lb_recv = 0;
      }

      dim_acc = ainfo[i].dim_acc;
      hi_src_offset += (off_t)lb_send * dim_acc * type_size;
      hi_dst_offset += (off_t)lb_recv * dim_acc * type_size;
      _XMP_TCA_DEBUG("[%d] lb_send = %d, lb_recv = %d, dim_acc = %llu\n", _XMP_world_rank, lb_send, lb_recv, dim_acc);
    }
  }

  off_t lo_src_offset = 0, lo_dst_offset = 0;
  if(uwidth){
    for(int i=0; i<ndims; i++){
      int lb_send, lb_recv;
      unsigned long long dim_acc;
      if (i == dim) {
        lb_send = ainfo[i].local_lower;
        lb_recv = ainfo[i].local_upper + 1;
      }
      else {
        // Note: including shadow area
        lb_send = 0;
        lb_recv = 0;
      }

      dim_acc = ainfo[i].dim_acc;
      lo_src_offset += (off_t)lb_send * dim_acc * type_size;
      lo_dst_offset += (off_t)lb_recv * dim_acc * type_size;
      _XMP_TCA_DEBUG("[%d] lb_send = %d, lb_recv = %d, dim_acc = %llu\n", _XMP_world_rank, lb_send, lb_recv, dim_acc);
    }
  }

  size_t lo_buf_size = 0, hi_buf_size = 0;
  lo_buf_size = lwidth * blocklength * count;
  hi_buf_size = uwidth * blocklength * count;

  TCA_CHECK(tcaMalloc((void**)&lo_send_device_buf, lo_buf_size, tcaMemoryGPU));
  TCA_CHECK(tcaMalloc((void**)&lo_recv_device_buf, lo_buf_size, tcaMemoryGPU));
  TCA_CHECK(tcaMalloc((void**)&hi_send_device_buf, hi_buf_size, tcaMemoryGPU));
  TCA_CHECK(tcaMalloc((void**)&hi_recv_device_buf, hi_buf_size, tcaMemoryGPU));

  create_tca_handle_list(&lo_send_handle, lo_send_device_buf, lo_buf_size);
  create_tca_handle_list(&lo_recv_handle, lo_recv_device_buf, lo_buf_size);
  create_tca_handle_list(&hi_send_handle, hi_send_device_buf, hi_buf_size);
  create_tca_handle_list(&hi_recv_handle, hi_recv_device_buf, hi_buf_size);

  reflect->lo_width = lwidth;
  reflect->hi_width = uwidth;
  reflect->count = count;
  reflect->blocklength = blocklength;
  reflect->stride = stride;
  reflect->lo_rank = lo_rank;
  reflect->hi_rank = hi_rank;
  reflect->lo_src_offset = lo_src_offset;
  reflect->lo_dst_offset = lo_dst_offset;
  reflect->hi_src_offset = hi_src_offset;
  reflect->hi_dst_offset = hi_dst_offset;
  reflect->lo_send_array = (char*)acc_addr + lo_src_offset;
  reflect->lo_recv_array = (char*)acc_addr + hi_dst_offset;
  reflect->hi_send_array = (char*)acc_addr + hi_src_offset;
  reflect->hi_recv_array = (char*)acc_addr + lo_dst_offset;
  reflect->lo_send_buf = lo_send_device_buf;
  reflect->lo_recv_buf = lo_recv_device_buf;
  reflect->hi_send_buf = hi_send_device_buf;
  reflect->hi_recv_buf = hi_recv_device_buf;
  reflect->lo_send_handle = lo_send_handle;
  reflect->lo_recv_handle = lo_recv_handle;
  reflect->hi_send_handle = hi_send_handle;
  reflect->hi_recv_handle = hi_recv_handle;

  reflect->lo_async_id = _XMP_alloc(sizeof(cudaStream_t));
  reflect->hi_async_id = _XMP_alloc(sizeof(cudaStream_t));
  cudaStreamCreate(reflect->lo_async_id);
  cudaStreamCreate(reflect->hi_async_id);
}

static void _XMP_create_TCA_reflect_desc(_XMP_array_t *adesc, void *acc_addr)
{
  static int dma_slot = 0;
  adesc->dma_slot = dma_slot;
  int array_dim = adesc->dim;
  
  for(int i=0;i<array_dim;i++){
    _XMP_array_info_t *ai = &(adesc->info[i]);
    if(ai->shadow_type == _XMP_N_SHADOW_NONE)
      continue;
    int is_periodic = _XMP_N_INT_FALSE; // FIX me
    _XMP_reflect_acc_sched_dim(adesc, acc_addr, i, is_periodic);
  }

  tcaHandle* h = (tcaHandle*)adesc->tca_handle;
  tcaDesc* tca_reflect_desc = (tcaDesc*)adesc->tca_reflect_desc;
  for(int i=0;i<array_dim;i++){
    _XMP_array_info_t *ai = &(adesc->info[i]);
    if(ai->shadow_type == _XMP_N_SHADOW_NONE)  continue;

    int lo_rank = ai->reflect_acc_sched->lo_rank;
    int hi_rank = ai->reflect_acc_sched->hi_rank;
    _XMP_reflect_sched_t *reflect = ai->reflect_acc_sched;
    int count = reflect->count;
    off_t lo_dst_offset = reflect->lo_dst_offset;
    off_t lo_src_offset = reflect->lo_src_offset;
    off_t hi_dst_offset = reflect->hi_dst_offset;
    off_t hi_src_offset = reflect->hi_src_offset;
    size_t width = reflect->blocklength;
    int wait_slot = adesc->wait_slot;
    int wait_tag = adesc->wait_tag;
    static int dma_flag = tcaDMAUseInternal|tcaDMAUseNotifyInternal|tcaDMANotify|tcaDMAPipeline;
    int dim_index;

    xmp_nodes_index(adesc->array_nodes, i+1, &dim_index);

    if(count == 1){
      if (dim_index % 2 == 0) {
	if (lo_rank != MPI_PROC_NULL) {
	  TCA_CHECK(tcaDescSetMemcpy(tca_reflect_desc, &h[lo_rank], lo_dst_offset, 
				     &h[_XMP_world_rank], lo_src_offset, width,
				     dma_flag, wait_slot, wait_tag));
	  _XMP_TCA_DEBUG("[%d, %d] lo tcaDescSetMemcpy form %d to %d rank, dma_slot = %d, wait_slot = %d, blocklength = %zu\n",
			 _XMP_world_rank, i, _XMP_world_rank, lo_rank, dma_slot, wait_slot, width);

	  lo_src_offset += reflect->stride;
	  lo_dst_offset += reflect->stride;
	}
	if (hi_rank != MPI_PROC_NULL) {
	  TCA_CHECK(tcaDescSetMemcpy(tca_reflect_desc, &h[hi_rank], hi_dst_offset,
				     &h[_XMP_world_rank], hi_src_offset, width,
				     dma_flag, wait_slot, wait_tag));
	  _XMP_TCA_DEBUG("[%d, %d] hi tcaDescSetMemcpy form %d to %d rank, dma_slot = %d, wait_slot = %d, blocklength = %zu\n",
			 _XMP_world_rank, i, _XMP_world_rank, hi_rank, dma_slot, wait_slot, width);

	  hi_src_offset += reflect->stride;
	  hi_dst_offset += reflect->stride;
	}
      } else {
	if (hi_rank != MPI_PROC_NULL) {
	  TCA_CHECK(tcaDescSetMemcpy(tca_reflect_desc, &h[hi_rank], hi_dst_offset,
				     &h[_XMP_world_rank], hi_src_offset, width,
				     dma_flag, wait_slot, wait_tag));
	  _XMP_TCA_DEBUG("[%d, %d] hi tcaDescSetMemcpy form %d to %d rank, dma_slot = %d, wait_slot = %d, blocklength = %zu\n",
			 _XMP_world_rank, i, _XMP_world_rank, hi_rank, dma_slot, wait_slot, width);
	  
	  hi_src_offset += reflect->stride;
	  hi_dst_offset += reflect->stride;
	}
	if (lo_rank != MPI_PROC_NULL) {
	  TCA_CHECK(tcaDescSetMemcpy(tca_reflect_desc, &h[lo_rank], lo_dst_offset, 
				     &h[_XMP_world_rank], lo_src_offset, width,
				     dma_flag, wait_slot, wait_tag));
	  _XMP_TCA_DEBUG("[%d, %d] lo tcaDescSetMemcpy form %d to %d rank, dma_slot = %d, wait_slot = %d, blocklength = %zu\n",
			 _XMP_world_rank, i, _XMP_world_rank, lo_rank, dma_slot, wait_slot, width);

	  lo_src_offset += reflect->stride;
	  lo_dst_offset += reflect->stride;
	}
      }
    }
    else if(count > 1){
      size_t pitch  = reflect->stride;
      if (width <= _XMP_TCA_USE_PACK_SIZE) { // Use Pack-Unpack
	size_t pack_width = width * count;
	if (dim_index % 2 == 0) {
	  if (lo_rank != MPI_PROC_NULL) {
	    tcaHandle *lo_send_handle = (tcaHandle*)reflect->lo_send_handle;
	    tcaHandle *hi_recv_handle = (tcaHandle*)reflect->hi_recv_handle;
	    TCA_CHECK(tcaDescSetMemcpy(tca_reflect_desc, &hi_recv_handle[lo_rank], 0,
				       &lo_send_handle[_XMP_world_rank], 0, pack_width,
				       dma_flag, wait_slot, wait_tag));
	    _XMP_TCA_DEBUG("[%d, %d] lo tcaDescSetMemcpy Pack form %d to %d rank, dma_slot = %d, wait_slot = %d, blocklength = %zu\n",
	    	   _XMP_world_rank, i, _XMP_world_rank, lo_rank, dma_slot, wait_slot, pack_width);
	  }
	  if (hi_rank != MPI_PROC_NULL) {
	    tcaHandle *hi_send_handle = (tcaHandle*)reflect->hi_send_handle;
	    tcaHandle *lo_recv_handle = (tcaHandle*)reflect->lo_recv_handle;
	    TCA_CHECK(tcaDescSetMemcpy(tca_reflect_desc, &lo_recv_handle[hi_rank], 0,
				       &hi_send_handle[_XMP_world_rank], 0, pack_width,
				       dma_flag, wait_slot, wait_tag));
	    _XMP_TCA_DEBUG("[%d, %d] hi tcaDescSetMemcpy Pack form %d to %d rank, dma_slot = %d, wait_slot = %d, blocklength = %zu\n",
	    	   _XMP_world_rank, i, _XMP_world_rank, hi_rank, dma_slot, wait_slot, pack_width);
	  }
	} else {
	  if (hi_rank != MPI_PROC_NULL) {
	    tcaHandle *hi_send_handle = (tcaHandle*)reflect->hi_send_handle;
	    tcaHandle *lo_recv_handle = (tcaHandle*)reflect->lo_recv_handle;
	    TCA_CHECK(tcaDescSetMemcpy(tca_reflect_desc, &lo_recv_handle[hi_rank], 0,
				       &hi_send_handle[_XMP_world_rank], 0, pack_width,
				       dma_flag, wait_slot, wait_tag));
	    _XMP_TCA_DEBUG("[%d, %d] hi tcaDescSetMemcpy Pack form %d to %d rank, dma_slot = %d, wait_slot = %d, blocklength = %zu\n",
	    	   _XMP_world_rank, i, _XMP_world_rank, hi_rank, dma_slot, wait_slot, pack_width);

	  }
	  if (lo_rank != MPI_PROC_NULL) {
	    tcaHandle *lo_send_handle = (tcaHandle*)reflect->lo_send_handle;
	    tcaHandle *hi_recv_handle = (tcaHandle*)reflect->hi_recv_handle;
	    TCA_CHECK(tcaDescSetMemcpy(tca_reflect_desc, &hi_recv_handle[lo_rank], 0,
				       &lo_send_handle[_XMP_world_rank], 0, pack_width,
				       dma_flag, wait_slot, wait_tag));
	    _XMP_TCA_DEBUG("[%d, %d] lo tcaDescSetMemcpy Pack form %d to %d rank, dma_slot = %d, wait_slot = %d, blocklength = %zu\n",
	    	   _XMP_world_rank, i, _XMP_world_rank, lo_rank, dma_slot, wait_slot, pack_width);
	  }
	}
      } else { // Use blockstride
	if (dim_index % 2 ==0) {
	  if(lo_rank != MPI_PROC_NULL){
	    TCA_CHECK(tcaDescSetMemcpy2D(tca_reflect_desc, &h[lo_rank], lo_dst_offset, pitch,
					 &h[_XMP_world_rank], lo_src_offset, pitch,
					 width, count, dma_flag, wait_slot, wait_tag));
	  }
	  if(hi_rank != MPI_PROC_NULL){
	    TCA_CHECK(tcaDescSetMemcpy2D(tca_reflect_desc, &h[hi_rank], hi_dst_offset, pitch,
					 &h[_XMP_world_rank], hi_src_offset, pitch,
					 width, count, dma_flag, wait_slot, wait_tag));
	  }
	} else {
	  if(hi_rank != MPI_PROC_NULL){
	    TCA_CHECK(tcaDescSetMemcpy2D(tca_reflect_desc, &h[hi_rank], hi_dst_offset, pitch,
					 &h[_XMP_world_rank], hi_src_offset, pitch,
					 width, count, dma_flag, wait_slot, wait_tag));
	  }
	  if(lo_rank != MPI_PROC_NULL){
	    TCA_CHECK(tcaDescSetMemcpy2D(tca_reflect_desc, &h[lo_rank], lo_dst_offset, pitch,
					 &h[_XMP_world_rank], lo_src_offset, pitch,
					 width, count, dma_flag, wait_slot, wait_tag));
	  }
	}
      }
    }
  }

  TCA_CHECK(tcaDescSet(tca_reflect_desc, _XMP_TCA_DMAC));
}

void _XMP_reflect_init_tca(void *acc_addr, _XMP_array_t *adesc)
{
  _XMP_create_TCA_handle(acc_addr, adesc);
  _XMP_create_TCA_reflect_desc(adesc, acc_addr);
}

// Unpack
static void gpu_unpack_vector(_XMP_reflect_sched_t *reflect, size_t type_size)
{
  int lo_width = reflect->lo_width;
  int hi_width = reflect->hi_width;
  long lo_buf_size = lo_width * reflect->blocklength;
  long hi_buf_size = hi_width * reflect->blocklength;

  if (lo_width && reflect->lo_rank != MPI_PROC_NULL) {
    _XMP_gpu_unpack_vector_async((char *)reflect->lo_recv_array,
				 (char *)reflect->lo_recv_buf,
				 reflect->count, lo_buf_size,
				 reflect->stride, type_size, reflect->lo_async_id);
  }

  if (hi_width && reflect->hi_rank != MPI_PROC_NULL) {
    _XMP_gpu_unpack_vector_async((char *)reflect->hi_recv_array,
				 (char *)reflect->hi_recv_buf,
				 reflect->count, hi_buf_size,
				 reflect->stride, type_size, reflect->hi_async_id);
  }
}

static void gpu_unpack_wait(_XMP_reflect_sched_t *reflect)
{
  if(reflect->lo_rank != MPI_PROC_NULL){
    _XMP_gpu_wait_async(reflect->lo_async_id);
  }
  if(reflect->hi_rank != MPI_PROC_NULL){
    _XMP_gpu_wait_async(reflect->hi_async_id);
  }
}

static void _XMP_tca_unpack_vector(_XMP_array_t *adesc)
{
  int packSkipDim = 0;
  if (_XMPF_running && !_XMPC_running) { /* for XMP/F */
    packSkipDim = adesc->dim - 1;
  } else if (!_XMPF_running && _XMPC_running) { /* for XMP/C */
    packSkipDim = 0;
  } else {
    _XMP_fatal("cannot determin the base language.");
  }

  for (int i = 0; i < adesc->dim; i++) {
    _XMP_array_info_t *ai = &(adesc->info[i]);
    if(! ai->is_shadow_comm_member)
      continue;
    _XMP_reflect_sched_t *reflect = ai->reflect_acc_sched;
    int lo_width = reflect->lo_width;
    int hi_width = reflect->hi_width;
    if (!lo_width && !hi_width)
      continue;

    if (ai->shadow_type == _XMP_N_SHADOW_NORMAL){
      if(i != packSkipDim && reflect->blocklength <= _XMP_TCA_USE_PACK_SIZE) {
	gpu_unpack_vector(reflect, adesc->type_size);
      }
    }
  }

  for (int i = 0; i < adesc->dim; i++) {
    _XMP_array_info_t *ai = &(adesc->info[i]);
    if (! ai->is_shadow_comm_member)
      continue;
    _XMP_reflect_sched_t *reflect = ai->reflect_acc_sched;
    int lo_width = reflect->lo_width;
    int hi_width = reflect->hi_width;
    if (!lo_width && !hi_width)
      continue;

    if (ai->shadow_type == _XMP_N_SHADOW_NORMAL) {
      if (i != packSkipDim && reflect->blocklength <= _XMP_TCA_USE_PACK_SIZE) {
	gpu_unpack_wait(reflect);
      }
    }
  }
}


// Pack
static void gpu_pack_vector(_XMP_reflect_sched_t *reflect, size_t type_size)
{
  int lo_width = reflect->lo_width;
  int hi_width = reflect->hi_width;
  long lo_buf_size = lo_width * reflect->blocklength;
  long hi_buf_size = hi_width * reflect->blocklength;

  if (lo_width && reflect->lo_rank != MPI_PROC_NULL) {
    if ((char *)reflect->lo_send_buf == NULL)
      _XMP_fatal("reflect->lo_send_buf is NULL\n");
    if ((char *)reflect->lo_send_array == NULL)
      _XMP_fatal("reflect->lo_send_array is NULL\n");
    if (reflect->lo_async_id == NULL)
      _XMP_fatal("reflect->lo_async_id is NULL\n");
    /* fprintf(stdout, "count = %d, lo_width = %d, blocklength = %d , stride = %lld, type_size = %zu\n", */
    /* 	    reflect->count, lo_width, reflect->blocklength, reflect->stride, type_size); */

    _XMP_gpu_pack_vector_async((char *)reflect->lo_send_buf,
			       (char *)reflect->lo_send_array,
			       reflect->count, lo_buf_size,
			       reflect->stride, type_size, reflect->lo_async_id);
  }
  
  if (hi_width && reflect->hi_rank != MPI_PROC_NULL) {
    if ((char *)reflect->hi_send_buf == NULL)
      _XMP_fatal("reflect->hi_send_buf is NULL\n");
    if ((char *)reflect->hi_send_array == NULL)
      _XMP_fatal("reflect->hi_send_array is NULL\n");
    if (reflect->hi_async_id == NULL)
      _XMP_fatal("reflect->hi_async_id is NULL\n");
    /* fprintf(stdout, "count = %d, hi_width = %d, bhicklength = %d , stride = %lld, type_size = %zu\n", */
    /* 	    reflect->count, hi_width, reflect->blocklength, reflect->stride, type_size); */

    _XMP_gpu_pack_vector_async((char *)reflect->hi_send_buf,
			       (char *)reflect->hi_send_array,
			       reflect->count, hi_buf_size,
			       reflect->stride, type_size, reflect->hi_async_id);
  }
}

static void gpu_pack_wait(_XMP_reflect_sched_t *reflect)
{
  if(reflect->lo_rank != MPI_PROC_NULL){
    _XMP_gpu_wait_async(reflect->lo_async_id);
  }
  if(reflect->hi_rank != MPI_PROC_NULL){
    _XMP_gpu_wait_async(reflect->hi_async_id);
  }
}

static void _XMP_tca_pack_vector(_XMP_array_t *adesc)
{
  int packSkipDim = 0;
  if (_XMPF_running && !_XMPC_running) { /* for XMP/F */
    packSkipDim = adesc->dim - 1;
  } else if (!_XMPF_running && _XMPC_running) { /* for XMP/C */
    packSkipDim = 0;
  } else {
    _XMP_fatal("cannot determin the base language.");
  }

  for (int i = 0; i < adesc->dim; i++) {
    _XMP_array_info_t *ai = &(adesc->info[i]);
    if(! ai->is_shadow_comm_member)
      continue;
    _XMP_reflect_sched_t *reflect = ai->reflect_acc_sched;
    int lo_width = reflect->lo_width;
    int hi_width = reflect->hi_width;
    if (!lo_width && !hi_width)
      continue;

    if (ai->shadow_type == _XMP_N_SHADOW_NORMAL){
      if(i != packSkipDim && reflect->blocklength <= _XMP_TCA_USE_PACK_SIZE) {
	gpu_pack_vector(reflect, adesc->type_size);
      }
    }
  }

  for(int i = 0; i < adesc->dim; i++){
    _XMP_array_info_t *ai = &(adesc->info[i]);
    if (! ai->is_shadow_comm_member)
      continue;
    _XMP_reflect_sched_t *reflect = ai->reflect_acc_sched;
    int lo_width = reflect->lo_width;
    int hi_width = reflect->hi_width;
    if (!lo_width && !hi_width)
      continue;

    if (ai->shadow_type == _XMP_N_SHADOW_NORMAL) {
      if (i != packSkipDim && reflect->blocklength <= _XMP_TCA_USE_PACK_SIZE) {
	gpu_pack_wait(reflect);
      }
    }
  }
}

static void _XMP_refect_wait_tca(_XMP_array_t *adesc)
{
  int array_dim = adesc->dim;
  tcaHandle* h = (tcaHandle*)adesc->tca_handle;
  int wait_slot = adesc->wait_slot;
  int wait_tag = adesc->wait_tag;
  
  for(int i=0;i<array_dim;i++){
    _XMP_array_info_t *ai = &(adesc->info[i]);
    if(ai->shadow_type == _XMP_N_SHADOW_NONE)
      continue;
    int lo_rank = ai->reflect_acc_sched->lo_rank;
    int hi_rank = ai->reflect_acc_sched->hi_rank;
    
    if(lo_rank != MPI_PROC_NULL)
      _XMP_TCA_DEBUG("[%d] lo wait from %d\n", _XMP_world_rank, lo_rank);
    if(hi_rank != MPI_PROC_NULL)
      _XMP_TCA_DEBUG("[%d] hi wait from %d\n", _XMP_world_rank, hi_rank);

    if(lo_rank != MPI_PROC_NULL)
      TCA_CHECK(tcaWaitDMARecvDesc(&h[lo_rank], wait_slot, wait_tag));

    if(hi_rank != MPI_PROC_NULL)
      TCA_CHECK(tcaWaitDMARecvDesc(&h[hi_rank], wait_slot, wait_tag));
  }
}

void _XMP_reflect_do_tca(_XMP_array_t *adesc)
{
  _XMP_tca_pack_vector(adesc);
  TCA_CHECK(tcaStartDMADesc(_XMP_TCA_DMAC));
  _XMP_refect_wait_tca(adesc);
  _XMP_tca_unpack_vector(adesc);
  MPI_Barrier(MPI_COMM_WORLD);
}
