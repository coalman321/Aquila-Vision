#include "object_detector_lib/kernels.h"

__global__ void gpu_matrix_mult(float *a, float *b, float *c, int m, int n, int k, int iVal, int numClasses) {
    int row = blockIdx.y * blockDim.y + threadIdx.y;
    int col = blockIdx.x * blockDim.x + threadIdx.x;
    float sum = 0;
    if (col < k && row < m) {
        for (int i = 0; i < n; i++) {
            sum += a[iVal + (4 + numClasses + i) * 8400] * b[i * k + col];
        }
        c[row * k + col] = sum;
    }
}

float sigmoid(float num) { return (float)((1 / (1 + exp(-num))) > 0.5); }