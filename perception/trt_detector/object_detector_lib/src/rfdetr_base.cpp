#include "object_detector_lib/rfdetr_base.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>

namespace object_detector {

namespace {

// rfdetr normalizes its input with the ImageNet statistics, in RGB order.
const cv::Scalar kMean(0.485, 0.456, 0.406);
const cv::Scalar kStd(0.229, 0.224, 0.225);

float *deviceBuffer(size_t count, const std::string &name) {
    float *buffer = nullptr;
    RfDetrBase::checkCuda(cudaMalloc(reinterpret_cast<void **>(&buffer), count * sizeof(float)), "malloc " + name);
    return buffer;
}

float sigmoid(float logit) { return 1.f / (1.f + std::exp(-logit)); }

}  // namespace

RfDetrBase::RfDetrBase(std::filesystem::path filepath, float conf_threshold) : confThreshold_(conf_threshold) {
    std::filesystem::path engine_path = getEnginePath(filepath);
    setupEngine(engine_path);

    for (int i = 0; i < engine_->getNbIOTensors(); ++i) {
        const char *name = engine_->getIOTensorName(i);
        if (engine_->getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT) {
            if (!inputName_.empty()) {
                throw std::runtime_error("RF-DETR model " + filepath.string() + " has more than one input");
            }
            inputName_ = name;
        }
    }
    if (inputName_.empty()) {
        throw std::runtime_error("RF-DETR model " + filepath.string() + " has no input");
    }

    // A dynamic batch dimension is pinned to 1, which also resolves the output shapes.
    nvinfer1::Dims inputDims = engine_->getTensorShape(inputName_.c_str());
    if (inputDims.nbDims != 4 || inputDims.d[1] != 3 || inputDims.d[2] <= 0 || inputDims.d[3] <= 0) {
        throw std::runtime_error("RF-DETR input " + inputName_ + " must be Nx3xHxW with a fixed H and W");
    }
    inputHeight_ = inputDims.d[2];
    inputWidth_ = inputDims.d[3];
    if (inputDims.d[0] != 1 && !context->setInputShape(inputName_.c_str(), nvinfer1::Dims4(1, 3, inputHeight_,
                                                                                             inputWidth_))) {
        throw std::runtime_error("Failed to set the input shape of " + inputName_);
    }
    tensorShape(inputName_.c_str(), 4);

    const nvinfer1::Dims detsDims = tensorShape("dets", 3);
    const nvinfer1::Dims labelsDims = tensorShape("labels", 3);
    numQueries_ = detsDims.d[1];
    numClasses_ = labelsDims.d[2];
    if (detsDims.d[2] != 4 || labelsDims.d[1] != numQueries_) {
        throw std::runtime_error("RF-DETR outputs must be dets 1xQx4 and labels 1xQxC");
    }

    const bool withMasks = engine_->getTensorIOMode("masks") == nvinfer1::TensorIOMode::kOUTPUT;
    if (withMasks) {
        const nvinfer1::Dims masksDims = tensorShape("masks", 4);
        if (masksDims.d[1] != numQueries_) {
            throw std::runtime_error("RF-DETR output masks must be 1xQxHxW");
        }
        maskHeight_ = masksDims.d[2];
        maskWidth_ = masksDims.d[3];
    }

    input_ = deviceBuffer(3 * inputHeight_ * inputWidth_, "input");
    dets_ = deviceBuffer(numQueries_ * 4, "dets");
    labels_ = deviceBuffer(numQueries_ * numClasses_, "labels");
    detsCopy_.resize(numQueries_ * 4);
    labelsCopy_.resize(numQueries_ * numClasses_);
    if (withMasks) {
        masks_ = deviceBuffer(numQueries_ * maskHeight_ * maskWidth_, "masks");
    }

    if (!context->setInputTensorAddress(inputName_.c_str(), input_)) {
        throw std::runtime_error("Failed to set input tensor");
    }
    if (!context->setTensorAddress("dets", dets_)) {
        throw std::runtime_error("Failed to set dets tensor");
    }
    if (!context->setTensorAddress("labels", labels_)) {
        throw std::runtime_error("Failed to set labels tensor");
    }
    if (withMasks && !context->setTensorAddress("masks", masks_)) {
        throw std::runtime_error("Failed to set masks tensor");
    }
}

nvinfer1::Dims RfDetrBase::tensorShape(const char *name, int rank) {
    if (engine_->getTensorIOMode(name) == nvinfer1::TensorIOMode::kNONE) {
        throw std::runtime_error(std::string("RF-DETR model has no tensor ") + name +
                                 " (is it an rfdetr export, not another architecture?)");
    }
    // An FP16 tensor would be read as garbage rather than fail, so refuse it up front.
    if (engine_->getTensorDataType(name) != nvinfer1::DataType::kFLOAT) {
        throw std::runtime_error(std::string("RF-DETR tensor ") + name + " must be FP32");
    }
    const nvinfer1::Dims dims = context->getTensorShape(name);
    if (dims.nbDims != rank || dims.d[0] != 1) {
        throw std::runtime_error(std::string("RF-DETR tensor ") + name + " must have rank " +
                                 std::to_string(rank) + " and a batch of 1");
    }
    for (int i = 1; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0) {
            throw std::runtime_error(std::string("RF-DETR tensor ") + name + " has a dynamic dimension");
        }
    }
    return dims;
}

int RfDetrBase::numClasses() { return numClasses_; }

void RfDetrBase::checkCuda(cudaError_t ret, const std::string &what) {
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to " + what + ": " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }
}

std::vector<RfDetrBase::Candidate> RfDetrBase::infer(const cv::Mat &image) {
    // Stretched to the model's resolution, not letterboxed: rfdetr trains and predicts that way.
    cv::Mat rgb, resized, normalized;
    cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
    cv::resize(rgb, resized, cv::Size(inputWidth_, inputHeight_), 0, 0, cv::INTER_LINEAR);
    resized.convertTo(normalized, CV_32FC3, 1. / 255.);
    cv::subtract(normalized, kMean, normalized);
    cv::divide(normalized, kStd, normalized);

    cv::Mat blob = cv::dnn::blobFromImage(normalized);
    checkCuda(cudaMemcpyAsync(input_, blob.data, 3 * inputHeight_ * inputWidth_ * sizeof(float),
                              cudaMemcpyHostToDevice, contextStream),
              "memcpy to input");

    if (!context->enqueueV3(contextStream)) {
        throw std::runtime_error("Failed to run inference\n");
    }
    checkCuda(cudaMemcpyAsync(detsCopy_.data(), dets_, detsCopy_.size() * sizeof(float), cudaMemcpyDeviceToHost,
                              contextStream),
              "memcpy from dets");
    checkCuda(cudaMemcpyAsync(labelsCopy_.data(), labels_, labelsCopy_.size() * sizeof(float),
                              cudaMemcpyDeviceToHost, contextStream),
              "memcpy from labels");
    checkCuda(cudaStreamSynchronize(contextStream), "run inference");

    // Every (query, class) pair is a candidate; like rfdetr, keep the best numQueries_ of them.
    std::vector<Candidate> candidates;
    for (int q = 0; q < numQueries_; ++q) {
        for (int c = 0; c < numClasses_; ++c) {
            const float score = sigmoid(labelsCopy_[q * numClasses_ + c]);
            if (score > confThreshold_) {
                candidates.push_back({ score, q, c });
            }
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    if (candidates.size() > static_cast<size_t>(numQueries_)) {
        candidates.resize(numQueries_);
    }
    return candidates;
}

cv::Rect RfDetrBase::bbox(const Candidate &candidate, const cv::Mat &image) const {
    const float *box = &detsCopy_[candidate.query * 4];
    const float width = box[2] * image.cols;
    const float height = box[3] * image.rows;
    return cv::Rect(cvRound(box[0] * image.cols - width / 2), cvRound(box[1] * image.rows - height / 2),
                    cvRound(width), cvRound(height));
}

RfDetrBase::~RfDetrBase() {
    // The TensorRT objects are released by ~DetectorCommon().
    cudaStreamDestroy(contextStream);
    cudaFree(input_);
    cudaFree(dets_);
    cudaFree(labels_);
    cudaFree(masks_);
}

}  // namespace object_detector
