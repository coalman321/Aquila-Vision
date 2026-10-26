#include "tools/cuda_smoke/vector_add.h"

#include <cuda_runtime.h>

namespace cuda_smoke {
namespace {

__global__ void VectorAddKernel(const float* a, const float* b, float* out, int n) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) {
    out[i] = a[i] + b[i];
  }
}

}  // namespace

cudaError_t VectorAdd(const float* a, const float* b, float* out, int n) {
  const size_t bytes = static_cast<size_t>(n) * sizeof(float);
  float* d_a = nullptr;
  float* d_b = nullptr;
  float* d_out = nullptr;
  cudaError_t err = cudaSuccess;

  if ((err = cudaMalloc(&d_a, bytes)) != cudaSuccess ||
      (err = cudaMalloc(&d_b, bytes)) != cudaSuccess ||
      (err = cudaMalloc(&d_out, bytes)) != cudaSuccess ||
      (err = cudaMemcpy(d_a, a, bytes, cudaMemcpyHostToDevice)) != cudaSuccess ||
      (err = cudaMemcpy(d_b, b, bytes, cudaMemcpyHostToDevice)) != cudaSuccess) {
    cudaFree(d_a);
    cudaFree(d_b);
    cudaFree(d_out);
    return err;
  }

  constexpr int kThreads = 256;
  VectorAddKernel<<<(n + kThreads - 1) / kThreads, kThreads>>>(d_a, d_b, d_out, n);
  err = cudaGetLastError();
  if (err == cudaSuccess) {
    err = cudaMemcpy(out, d_out, bytes, cudaMemcpyDeviceToHost);
  }

  cudaFree(d_a);
  cudaFree(d_b);
  cudaFree(d_out);
  return err;
}

}  // namespace cuda_smoke
