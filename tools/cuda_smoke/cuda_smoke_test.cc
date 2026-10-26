#include <cuda_runtime_api.h>

#include <cstdio>
#include <string>
#include <vector>

#include "tools/cuda_smoke/thrust_reduce.h"
#include "tools/cuda_smoke/vector_add.h"
#include "tools/cuda_test/gpu_available.h"

// Checks the linked runtime is the pinned CUDA 13.2 everywhere, and runs a kernel only where a GPU
// is visible and the build has kernels for it: CI runners and the devcontainer have no GPU, a Jetson
// Thor does.
int main() {
  int runtime_version = 0;
  if (cudaRuntimeGetVersion(&runtime_version) != cudaSuccess || runtime_version != 13020) {
    std::fprintf(stderr, "expected CUDA runtime 13020, got %d\n", runtime_version);
    return 1;
  }

  std::string reason;
  if (!cuda_test::GpuAvailable(&reason)) {
    std::printf("%s; skipping kernel launch\n", reason.c_str());
    return 0;
  }

  constexpr int kN = 1 << 20;
  std::vector<float> a(kN), b(kN), out(kN);
  for (int i = 0; i < kN; ++i) {
    a[i] = static_cast<float>(i);
    b[i] = 2.0f * static_cast<float>(i);
  }

  const cudaError_t err = cuda_smoke::VectorAdd(a.data(), b.data(), out.data(), kN);
  if (err != cudaSuccess) {
    std::fprintf(stderr, "VectorAdd failed: %s\n", cudaGetErrorString(err));
    return 1;
  }
  for (int i = 0; i < kN; ++i) {
    if (out[i] != 3.0f * static_cast<float>(i)) {
      std::fprintf(stderr, "out[%d] = %f, expected %f\n", i, out[i], 3.0f * static_cast<float>(i));
      return 1;
    }
  }

  std::vector<float> values = {3.0f, -1.0f, 2.5f, 0.0f, 7.0f};
  float sum = 0.0f;
  const cudaError_t sort_err = cuda_smoke::SortAndSum(values.data(), values.size(), &sum);
  if (sort_err != cudaSuccess) {
    std::fprintf(stderr, "SortAndSum failed: %s\n", cudaGetErrorString(sort_err));
    return 1;
  }
  if (values != std::vector<float>{-1.0f, 0.0f, 2.5f, 3.0f, 7.0f} || sum != 11.5f) {
    std::fprintf(stderr, "SortAndSum gave the wrong order or sum %f\n", sum);
    return 1;
  }
  std::printf("VectorAdd and SortAndSum verified\n");
  return 0;
}
