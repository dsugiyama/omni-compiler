/*
 * $TSUKUBA_Release: $
 * $TSUKUBA_Copyright:
 *  $
 */

#ifndef _XMP_INTERNAL
#define _XMP_INTERNAL

extern int _XMPC_running;
extern int _XMPF_running;

// --------------- including headers  --------------------------------
#include <mpi.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
// --------------- macro functions -----------------------------------
#ifdef DEBUG
#define _XMP_ASSERT(_flag) \
{ \
  if (!(_flag)) { \
    _XMP_unexpected_error(); \
  } \
}
#else
#define _XMP_ASSERT(_flag)
#endif

#define _XMP_RETURN_IF_SINGLE \
{ \
  if (_XMP_world_size == 1) { \
    return; \
  } \
}

#define _XMP_IS_SINGLE \
(_XMP_world_size == 1)

// --------------- structures ----------------------------------------
#include "xmp_data_struct.h"

#ifdef __cplusplus
extern "C" {
#endif

// ----- libxmp ------------------------------------------------------
// xmp_align.c
extern void _XMP_calc_array_dim_elmts(_XMP_array_t *array, int array_index);
extern void _XMP_align_array_DUPLICATION(_XMP_array_t *array, int array_index, int template_index,
                                  long long align_subscript);
extern void _XMP_align_array_BLOCK(_XMP_array_t *array, int array_index, int template_index,
                            long long align_subscript, int *temp0);
extern void _XMP_align_array_CYCLIC(_XMP_array_t *array, int array_index, int template_index,
                             long long align_subscript, int *temp0);
extern void _XMP_align_array_BLOCK_CYCLIC(_XMP_array_t *array, int array_index, int template_index,
                                   long long align_subscript, int *temp0);
extern void _XMP_init_array_nodes(_XMP_array_t *array);

// xmp_array_section.c
extern void _XMP_normalize_array_section(int *lower, int *upper, int *stride);
// FIXME make these static
extern void _XMP_pack_array_BASIC(void *buffer, void *src, int array_type,
                                         int array_dim, int *l, int *u, int *s, unsigned long long *d);
extern void _XMP_pack_array_GENERAL(void *buffer, void *src, size_t array_type_size,
                                           int array_dim, int *l, int *u, int *s, unsigned long long *d);
extern void _XMP_unpack_array_BASIC(void *dst, void *buffer, int array_type,
                                           int array_dim, int *l, int *u, int *s, unsigned long long *d);
extern void _XMP_unpack_array_GENERAL(void *dst, void *buffer, size_t array_type_size,
                                             int array_dim, int *l, int *u, int *s, unsigned long long *d);
extern void _XMP_pack_array(void *buffer, void *src, int array_type, size_t array_type_size,
                            int array_dim, int *l, int *u, int *s, unsigned long long *d);
extern void _XMP_unpack_array(void *dst, void *buffer, int array_type, size_t array_type_size,
                              int array_dim, int *l, int *u, int *s, unsigned long long *d);

// xmp_barrier.c
extern void _XMP_barrier_EXEC(void);

// xmp_coarray.c
typedef struct _XMP_coarray_list_type {
  _XMP_coarray_t *coarray;
  struct _XMP_coarray_list_type *next;
} _XMP_coarray_list_t;

extern _XMP_coarray_list_t *_XMP_coarray_list_head;
extern _XMP_coarray_list_t *_XMP_coarray_list_tail;

extern void _XMP_coarray_initialize(int, char **);
extern void _XMP_coarray_finalize(int);

// xmp_loop.c
extern int _XMP_sched_loop_template_width_1(int ser_init, int ser_cond, int ser_step,
                                            int *par_init, int *par_cond, int *par_step,
                                            int template_lower, int template_upper, int template_stride);
extern int _XMP_sched_loop_template_width_N(int ser_init, int ser_cond, int ser_step,
                                            int *par_init, int *par_cond, int *par_step,
                                            int template_lower, int template_upper, int template_stride,
                                            int width, int template_ser_lower, int template_ser_upper);

// xmp_nodes.c
extern _XMP_nodes_t *_XMP_init_nodes_struct_GLOBAL(int dim, int *dim_size, int is_static);
extern _XMP_nodes_t *_XMP_init_nodes_struct_EXEC(int dim, int *dim_size, int is_static);
extern _XMP_nodes_t *_XMP_init_nodes_struct_NODES_NUMBER(int dim, int ref_lower, int ref_upper, int ref_stride,
                                                         int *dim_size, int is_static);
extern _XMP_nodes_t *_XMP_init_nodes_struct_NODES_NAMED(int dim, _XMP_nodes_t *ref_nodes,
                                                        int *shrink, int *ref_lower, int *ref_upper, int *ref_stride,
                                                        int *dim_size, int is_static);
extern void _XMP_finalize_nodes(_XMP_nodes_t *nodes);
extern _XMP_nodes_t *_XMP_create_nodes_by_comm(int is_member, _XMP_comm_t *comm);
extern int _XMP_calc_linear_rank(_XMP_nodes_t *n, int *rank_array);
extern int _XMP_calc_linear_rank_on_target_nodes(_XMP_nodes_t *n, int *rank_array, _XMP_nodes_t *target_nodes);
extern _XMP_nodes_ref_t *_XMP_init_nodes_ref(_XMP_nodes_t *n, int *rank_array);
extern void _XMP_finalize_nodes_ref(_XMP_nodes_ref_t *nodes_ref);
extern _XMP_nodes_ref_t *_XMP_create_nodes_ref_for_target_nodes(_XMP_nodes_t *n, int *rank_array, _XMP_nodes_t *target_nodes);
extern void _XMP_translate_nodes_rank_array_to_ranks(_XMP_nodes_t *nodes, int *ranks, int *rank_array, int shrink_nodes_size);
extern int _XMP_get_next_rank(_XMP_nodes_t *nodes, int *rank_array);
extern int _XMP_calc_nodes_index_from_inherit_nodes_index(_XMP_nodes_t *nodes, int inherit_nodes_index);

// xmp_nodes_stack.c
extern void _XMP_push_nodes(_XMP_nodes_t *nodes);
extern void _XMP_pop_nodes(void);
extern void _XMP_pop_n_free_nodes(void);
extern void _XMP_pop_n_free_nodes_wo_finalize_comm(void);
extern _XMP_nodes_t *_XMP_get_execution_nodes(void);
extern int _XMP_get_execution_nodes_rank(void);
extern void _XMP_push_comm(_XMP_comm_t *comm);
extern void _XMP_finalize_comm(_XMP_comm_t *comm);

// xmp_shadow.c
extern void _XMP_create_shadow_comm(_XMP_array_t *array, int array_index);

// xmp_template.c
extern _XMP_template_t *_XMP_create_template_desc(int dim, _Bool is_fixed);
extern int _XMP_check_template_ref_inclusion(int ref_lower, int ref_upper, int ref_stride,
                                             _XMP_template_t *t, int index);
extern _XMP_nodes_t *_XMP_create_nodes_by_template_ref(_XMP_template_t *ref_template, int *shrink,
                                                       long long *ref_lower, long long *ref_upper, long long *ref_stride);

extern void _XMP_calc_template_size(_XMP_template_t *t);
extern void _XMP_init_template_chunk(_XMP_template_t *template, _XMP_nodes_t *nodes);
extern int _XMP_calc_template_owner_SCALAR(_XMP_template_t *ref_template, int dim_index, long long ref_index);
extern int _XMP_calc_template_par_triplet(_XMP_template_t *template, int template_index, int nodes_rank,
                                          int *template_lower, int *template_upper, int *template_stride);

void _XMP_dist_template_DUPLICATION(_XMP_template_t *template, int template_index);
void _XMP_dist_template_BLOCK(_XMP_template_t *template, int template_index, int nodes_index);
void _XMP_dist_template_CYCLIC(_XMP_template_t *template, int template_index, int nodes_index) ;
void _XMP_dist_template_BLOCK_CYCLIC(_XMP_template_t *template, int template_index, int nodes_index, unsigned long long width);


// xmp_util.c
extern unsigned long long _XMP_get_on_ref_id(void);
extern void *_XMP_alloc(size_t size);
extern void _XMP_free(void *p);
extern void _XMP_fatal(char *msg);
extern void _XMP_unexpected_error(void);

// xmp_world.c
extern int _XMP_world_size;
extern int _XMP_world_rank;
extern void *_XMP_world_nodes;

extern void _XMP_init_world(int *argc, char ***argv);
extern void _XMP_finalize_world(void);
extern int _XMP_split_world_by_color(int color);

// ----- libxmp_threads ----------------------------------------------
// xmp_threads_runtime.c
extern void _XMP_threads_init(void);
extern void _XMP_threads_finalize(void);

#ifdef __cplusplus
}
#endif


// ----- for coarray -------------------
#if defined(_XMP_COARRAY_FJRDMA) || defined(_XMP_COARRAY_GASNET)
#define _XMP_DEFAULT_COARRAY_HEAP_SIZE (256*1024*1024)  // 256MB
#define _XMP_DEFAULT_COARRAY_STRIDE_SIZE (16*1024*1024)  // 16MB
#endif

extern void _XMP_post_initialize();
#ifdef _XMP_COARRAY_GASNET
#include <gasnet.h>
#define _XMP_GASNET_STRIDE_INIT_SIZE 16
#define _XMP_GASNET_STRIDE_BLK       16
#define _XMP_GASNET_ALIGNMENT        8

#define GASNET_BARRIER() do {      \
	gasnet_barrier_notify(0,GASNET_BARRIERFLAG_ANONYMOUS);            \
	gasnet_barrier_wait(0,GASNET_BARRIERFLAG_ANONYMOUS);      \
  } while (0)

extern void _XMP_gasnet_set_coarray(_XMP_coarray_t *coarray, void **addr, unsigned long long number_of_elements, size_t type_size);
extern void _XMP_gasnet_initialize(int, char**);
extern void _XMP_gasnet_finalize(int);
extern void _XMP_gasnet_put(int, int, int, int, int, _XMP_array_section_t*, _XMP_array_section_t*, 
			    _XMP_coarray_t*, void*, long long);
extern void _XMP_gasnet_get(int, int, int, int, int, _XMP_array_section_t*, _XMP_array_section_t*,
                            _XMP_coarray_t*, void*, long long);
extern void _XMP_gasnet_sync_all();
extern void _XMP_gasnet_sync_memory();
extern void _xmp_gasnet_post_initialize();
extern void _xmp_gasnet_post(int, int);
extern void _xmp_gasnet_wait(int, ...);
#endif

// ---- for rdma ----
#ifdef _XMP_COARRAY_FJRDMA
#include <mpi-ext.h>

extern int _XMP_fjrdma_initialize();
extern int _XMP_fjrdma_finalize();
extern void _XMP_fjrdma_sync_memory();
extern void _XMP_fjrdma_sync_all();
extern int get_memid();
extern uint64_t _XMP_fjrdma_local_set(_XMP_coarray_t *coarray,
				      void **buf,
				      unsigned long long num_of_elmts);
extern void _XMP_fjrdma_put(int target_image,
			    int dest_continuous,
			    int src_continuous,
			    int dest_dims, int src_dims,
			    _XMP_array_section_t *dest_info, 
			    _XMP_array_section_t *src_info,
			    _XMP_coarray_t *dest,
			    void *src,
			    long long length,
			    long long,
			    int*);
extern void _XMP_fjrdma_get(int target_image,
			    int dest_continuous,
			    int src_continuous,
			    int dest_dims, int src_dims,
			    _XMP_array_section_t *dest_info, 
			    _XMP_array_section_t *src_info,
			    _XMP_coarray_t *dest,
			    void *src,
			    long long length,
			    long long,
			    int*);
#endif
extern uint64_t get_addr(uint64_t, int, _XMP_array_section_t*, int);
extern unsigned long long _xmp_heap_size, _xmp_stride_size;
#endif // _XMP_INTERNAL

