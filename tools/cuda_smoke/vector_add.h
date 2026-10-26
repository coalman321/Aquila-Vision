#pragma once

#include <cuda_runtime_api.h>

namespace cuda_smoke {

// Computes out[i] = a[i] + b[i] on the GPU. Pointers are host memory.
cudaError_t VectorAdd(const float* a, const float* b, float* out, int n);

}  // namespace cuda_smoke
