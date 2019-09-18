#ifndef TINKER_GPU_CARD_H_
#define TINKER_GPU_CARD_H_

#include "rc_man.h"

TINKER_NAMESPACE_BEGIN
/// \defgroup nv Nvidia GPU
/// \ingroup gvar

/// \ingroup nv
/// Number of threads in a warp.
constexpr unsigned WARP_SIZE = 32;

/// \ingroup nv
/// Maximum number of threads allowed in a thread block.
constexpr unsigned MAX_BLOCK_SIZE = 256;
TINKER_EXTERN int ndevice, idevice;

void gpu_card_data(rc_op op);
int get_grid_size(int nthreads_per_block);
int get_block_size(int shared_bytes_per_thread);
TINKER_NAMESPACE_END

#endif