#include "tools/cuda_smoke/thrust_reduce.h"

#include <cub/device/device_reduce.cuh>
#include <thrust/copy.h>
#include <thrust/device_vector.h>
#include <thrust/sort.h>

namespace cuda_smoke {

cudaError_t SortAndSum(float* values, int n, float* sum) {
  thrust::device_vector<float> d_values(values, values + n);
  thrust::sort(d_values.begin(), d_values.end());
  thrust::copy(d_values.begin(), d_values.end(), values);

  thrust::device_vector<float> d_sum(1);
  const float* in = thrust::raw_pointer_cast(d_values.data());
  float* out = thrust::raw_pointer_cast(d_sum.data());
  size_t temp_bytes = 0;
  cudaError_t err = cub::DeviceReduce::Sum(nullptr, temp_bytes, in, out, n);
  if (err != cudaSuccess) {
    return err;
  }
  thrust::device_vector<unsigned char> temp(temp_bytes);
  err = cub::DeviceReduce::Sum(thrust::raw_pointer_cast(temp.data()), temp_bytes, in, out, n);
  if (err != cudaSuccess) {
    return err;
  }
  *sum = d_sum[0];
  return cudaSuccess;
}

}  // namespace cuda_smoke
