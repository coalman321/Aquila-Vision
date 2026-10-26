#include <NvInferVersion.h>
#include <NvOnnxParser.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <opencv2/dnn.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include "object_detector_lib/kernels.h"
#include "object_detector_lib/yolov8_segment.hpp"

#define BLOCK_SIZE 32

namespace object_detector {

int YoloV8Segmenter::numClasses() { return numClasses_; }

YoloV8Segmenter::YoloV8Segmenter(std::filesystem::path filepath, float conf_threshold, float iou_threshold) :
    confThreshold_(conf_threshold), iouThreshold_(iou_threshold) {
    std::filesystem::path engine_path = getEnginePath(filepath);
    setupEngine(engine_path);

    numClasses_ = engine_->getTensorShape("output0").d[1] - 4 - 32;
    cudaDeviceSynchronize();

    output0Copy = new float[8400 * (numClasses_ + 4 + 32)];
    output1Copy = new float[160 * 160 * 32];

    cudaError_t ret = cudaMallocManaged((void **)(&input), 640 * 640 * 3 * sizeof(float));
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to malloc input: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    ret = cudaMallocManaged((void **)(&output0), 8400 * (numClasses_ + 4 + 32) * sizeof(float));
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to malloc output0: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    ret = cudaMallocManaged((void **)(&output1), 160 * 160 * 32 * sizeof(float));
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to malloc output1: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    ret = cudaMallocManaged((void **)(&maskWeights), 32 * sizeof(float));
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to malloc maskWeights: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    ret = cudaMallocManaged((void **)(&mask), 160 * 160 * sizeof(float));
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to malloc mask: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    if (!context->setInputTensorAddress("images", input)) {
        throw std::runtime_error("Failed to set input tensor");
    }

    if (!context->setTensorAddress("output0", output0)) {
        throw std::runtime_error("Failed to set output0 tensor");
    }

    if (!context->setTensorAddress("output1", output1)) {
        throw std::runtime_error("Failed to set output1 tensor");
    }
}

std::vector<Detection> YoloV8Segmenter::runDetection(cv::Mat &image) {
    using namespace std::chrono;

    cv::Mat resized, preprocessed;

    float aspectRatio, colRatio, rowRatio;
    int topBorder, sideBorder;
    bool widthLarger = false;

    if (image.cols > image.rows) {
        aspectRatio = (float)image.rows / image.cols;
        widthLarger = true;
    } else {
        aspectRatio = (float)image.cols / image.rows;
    }

    cv::Size newSize;
    if (widthLarger) {
        newSize = cv::Size(640, 640 * aspectRatio);
        topBorder = (float)(640. - newSize.height) / 2;
        sideBorder = 0;
    } else {
        newSize = cv::Size(640 * aspectRatio, 640);
        topBorder = 0;
        sideBorder = (float)(640. - newSize.width) / 2;
    }
    colRatio = (float)image.cols / newSize.width;
    rowRatio = (float)image.rows / newSize.height;

    cv::resize(image, resized, newSize, 0, 0, cv::INTER_LINEAR);
    // The far borders take any odd pixel, so the input is always exactly 640x640.
    cv::copyMakeBorder(resized, preprocessed, topBorder, 640 - newSize.height - topBorder, sideBorder,
                       640 - newSize.width - sideBorder, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

    preprocessed.convertTo(preprocessed, CV_32FC3);
    preprocessed /= 255.;

    cv::Mat blob = cv::dnn::blobFromImage(preprocessed);
    cudaError_t ret = cudaMemcpy(input, blob.data, 640 * 640 * 3 * sizeof(float), cudaMemcpyHostToDevice);
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to memcpy to input: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    bool success = context->enqueueV3(contextStream);
    cudaStreamSynchronize(contextStream);
    if (!success) {
        throw std::runtime_error("Failed to run inference\n");
    }

    ret = cudaMemcpy(output0Copy, output0, 8400 * (numClasses_ + 4 + 32) * 4, cudaMemcpyDeviceToHost);
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to memcpy to output0Copy: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    std::vector<std::vector<cv::Rect>> classDetections(numClasses_);
    std::vector<std::vector<float>> classScores(numClasses_);
    std::vector<std::vector<int>> iValues(numClasses_);
    std::vector<Detection> detections;
    for (int i = 0; i < 8400; i++) {
        // Detection detection;
        float maxConf = -1;
        int classId = 0;
        for (int j = 0; j < numClasses_; j++) {
            float conf = output0Copy[i + (j + 4) * 8400];
            if (conf > maxConf) {
                classId = j;
                maxConf = conf;
            }
        }
        if (maxConf > confThreshold_) {
            float normBbox[4];
            normBbox[0] = (output0Copy[i] - sideBorder) * colRatio;
            normBbox[1] = (output0Copy[i + 8400] - topBorder) * rowRatio;
            normBbox[2] = output0Copy[i + 2 * 8400] * colRatio;
            normBbox[3] = output0Copy[i + 3 * 8400] * rowRatio;
            cv::Rect bbox =
                cv::Rect(normBbox[0] - normBbox[2] / 2, normBbox[1] - normBbox[3] / 2, normBbox[2], normBbox[3]);
            classDetections[classId].push_back(bbox);
            classScores[classId].push_back(maxConf);
            iValues[classId].push_back(i);
        }
    }

    // Each mask is scaled up to cover the whole letterboxed input at the image's scale, so the borders
    // have to be scaled with it. Clamped so the crop stays inside the mask whatever the rounding.
    const int maskSize = widthLarger ? image.cols : image.rows;
    const double maskScale = maskSize / 640.;
    const int maskTop = std::min<int>(std::lround(topBorder * maskScale), maskSize - image.rows);
    const int maskSide = std::min<int>(std::lround(sideBorder * maskScale), maskSize - image.cols);

    for (int i = 0; i < numClasses_; i++) {
        std::vector<int> indices;
        cv::dnn::NMSBoxes(classDetections[i], classScores[i], confThreshold_, iouThreshold_, indices);
        for (int index : indices) {
            cv::Mat calculatedMask = calculateMask(iValues[i][index]);
            cv::resize(calculatedMask, calculatedMask, cv::Size(maskSize, maskSize));

            cv::Rect detectionRect =
                cv::Rect(classDetections[i][index].x + maskSide, classDetections[i][index].y + maskTop,
                         classDetections[i][index].width, classDetections[i][index].height) &
                cv::Rect(0, 0, calculatedMask.cols, calculatedMask.rows);
            cv::Rect croppedRect = cv::Rect(classDetections[i][index].x, classDetections[i][index].y,
                                            detectionRect.width, detectionRect.height);
            classDetections[i][index] = croppedRect;
            cv::Mat roi = cv::Mat::zeros(calculatedMask.size(), calculatedMask.type());
            cv::rectangle(roi, detectionRect, cv::Scalar(1), -1);

            calculatedMask.setTo(cv::Scalar(0), roi != 1);

            calculatedMask = calculatedMask(cv::Rect(maskSide, maskTop, image.cols, image.rows));

            cv::cvtColor(calculatedMask * 255, calculatedMask, cv::COLOR_GRAY2BGR);
            calculatedMask.convertTo(calculatedMask, CV_8UC3);
            detections.push_back(Detection(i, classScores[i][index], classDetections[i][index], calculatedMask));
        }
    }

    return detections;
}

cv::Mat YoloV8Segmenter::calculateMask(int iVal) {
    unsigned int gridRows = (1 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    unsigned int gridCols = (160 * 160 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    dim3 dimGrid(gridCols, gridRows);
    dim3 dimBlock(BLOCK_SIZE, BLOCK_SIZE);
    gpu_matrix_mult<<<dimGrid, dimBlock>>>(output0, output1, mask, 1, 32, 160 * 160, iVal, numClasses_);
    cudaDeviceSynchronize();
    cv::Mat cpuMask(160, 160, CV_32FC1);

    cudaMemcpy(cpuMask.data, mask, 160 * 160 * sizeof(float), cudaMemcpyDeviceToHost);

    std::transform(cpuMask.begin<float>(), cpuMask.end<float>(), cpuMask.begin<float>(), sigmoid);

    return cpuMask;
}

YoloV8Segmenter::~YoloV8Segmenter() {
    // The TensorRT objects are released by ~DetectorCommon().
    cudaStreamDestroy(contextStream);
    cudaFree(input);
    cudaFree(output0);
    cudaFree(output1);
    cudaFree(maskWeights);
    cudaFree(mask);

    delete[] output0Copy;
    delete[] output1Copy;
}

}  // namespace object_detector
