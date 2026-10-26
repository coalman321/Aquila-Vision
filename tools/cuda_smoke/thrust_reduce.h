#pragma once

#include <cuda_runtime_api.h>

namespace cuda_smoke {

// Sorts `values` (host memory) on the GPU with Thrust and returns their sum computed with CUB, so both
// halves of CCCL are compiled and run.
cudaError_t SortAndSum(float* values, int n, float* sum);

}  // namespace cuda_smoke
