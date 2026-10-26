#pragma once

__global__ void gpu_matrix_mult(float *a, float *b, float *c, int m, int n, int k, int iVal, int numClasses);

float sigmoid(float num);