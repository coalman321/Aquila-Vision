#include "tools/cuda_test/gpu_available.h"

#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>

namespace cuda_test {
namespace {

__global__ void ProbeKernel() {}

bool CanRunKernels(std::string* reason) {
  int device_count = 0;
  const cudaError_t count_err = cudaGetDeviceCount(&device_count);
  if (count_err != cudaSuccess || device_count == 0) {
    *reason = std::string("no CUDA device (") + cudaGetErrorName(count_err) + ")";
    return false;
  }

  // A GPU the build has no cubin or JIT-able PTX for fails here with "no kernel image is available"
  // (not a sticky error, so the context stays usable). The default archs are Thor's; a desktop GPU
  // needs e.g. --config=sm86 (see .bazelrc).
  ProbeKernel<<<1, 1>>>();
  cudaError_t err = cudaGetLastError();
  if (err == cudaSuccess) {
    err = cudaDeviceSynchronize();
  }
  if (err != cudaSuccess) {
    int device = 0;
    cudaDeviceProp prop{};
    cudaGetDevice(&device);
    cudaGetDeviceProperties(&prop, device);
    *reason = std::string("CUDA device ") + prop.name + " (sm_" + std::to_string(prop.major) +
              std::to_string(prop.minor) + ") can't run this build's kernels (" +
              cudaGetErrorString(err) + "); build for its architecture, e.g. --config=sm86";
    return false;
  }
  return true;
}

}  // namespace

bool GpuAvailable(std::string* reason) {
  if (CanRunKernels(reason)) {
    return true;
  }
  // Set where a GPU is expected (CI's Thor runner), so losing it fails the tests instead of letting
  // them skip and still report PASSED.
  const char* require = std::getenv("CUDA_TEST_REQUIRE_GPU");
  if (require != nullptr && *require != '\0' && std::string(require) != "0") {
    std::fprintf(stderr, "CUDA_TEST_REQUIRE_GPU is set, but %s\n", reason->c_str());
    std::exit(1);
  }
  return false;
}

}  // namespace cuda_test
