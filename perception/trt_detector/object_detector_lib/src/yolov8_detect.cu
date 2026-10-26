#include <NvInferVersion.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <opencv2/dnn.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include "object_detector_lib/kernels.h"
#include "object_detector_lib/yolov8_detect.hpp"

#define BLOCK_SIZE 32

namespace object_detector {

int YoloV8Detector::numClasses() { return numClasses_; }

YoloV8Detector::YoloV8Detector(std::filesystem::path filepath, float conf_threshold, float iou_threshold) :
    confThreshold_(conf_threshold), iouThreshold_(iou_threshold) {
    std::filesystem::path engine_path = getEnginePath(filepath);

    setupEngine(engine_path);

    numClasses_ = engine_->getTensorShape("output0").d[1] - 4;
    cudaDeviceSynchronize();

    output0Copy = new float[8400 * (numClasses_ + 4)];

    cudaError_t ret = cudaMallocManaged((void **)(&input), 640 * 640 * 3 * sizeof(float));
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to malloc input: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    ret = cudaMallocManaged((void **)(&output0), 8400 * (numClasses_ + 4) * sizeof(float));
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to malloc output0: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    if (!context->setInputTensorAddress("images", input)) {
        throw std::runtime_error("Failed to set input tensor");
    }

    if (!context->setTensorAddress("output0", output0)) {
        throw std::runtime_error("Failed to set output0 tensor");
    }
}

std::vector<Detection> YoloV8Detector::runDetection(cv::Mat &image) {
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

    ret = cudaMemcpy(output0Copy, output0, 8400 * (numClasses_ + 4) * sizeof(float), cudaMemcpyDeviceToHost);
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to memcpy to output0Copy: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }

    std::vector<std::vector<cv::Rect>> classDetections(numClasses_);
    std::vector<std::vector<float>> classScores(numClasses_);
    std::vector<std::vector<int>> iValues(numClasses_);
    std::vector<Detection> detections;
    for (int i = 0; i < 8400; i++) {
        float maxConf = -1;
        int classId = 0;

        // Look for the most confident class of a given bbox
        for (int j = 0; j < numClasses_; j++) {
            float conf = output0Copy[i + (j + 4) * 8400];
            if (conf > maxConf) {
                classId = j;
                maxConf = conf;
            }
        }

        // this builds the boox for the given class
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

    for (int i = 0; i < numClasses_; i++) {
        std::vector<int> indices;
        cv::dnn::NMSBoxes(classDetections[i], classScores[i], confThreshold_, iouThreshold_, indices);
        for (int index : indices) {
            cv::Rect detectionRect =
                cv::Rect(classDetections[i][index].x + sideBorder, classDetections[i][index].y + topBorder,
                         classDetections[i][index].width, classDetections[i][index].height);
            cv::Rect croppedRect = cv::Rect(classDetections[i][index].x, classDetections[i][index].y,
                                            detectionRect.width, detectionRect.height);
            classDetections[i][index] = croppedRect;
            detections.push_back(Detection(i, classScores[i][index], classDetections[i][index]));
        }
    }

    return detections;
}

YoloV8Detector::~YoloV8Detector() {
    // The TensorRT objects are released by ~DetectorCommon().
    cudaStreamDestroy(contextStream);
    cudaFree(input);
    cudaFree(output0);

    delete[] output0Copy;
}

}  // namespace object_detector
