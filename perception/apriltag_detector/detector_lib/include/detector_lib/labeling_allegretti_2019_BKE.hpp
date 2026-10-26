#pragma once
#include <cuda_runtime.h>
#include <stdint.h>

#include "detector_lib/gpu_image.hpp"

void LabelImage(const GpuImage<uint8_t> input, GpuImage<uint32_t> output,
                GpuImage<uint32_t> union_markers_size_device,
                cudaStream_t stream);


