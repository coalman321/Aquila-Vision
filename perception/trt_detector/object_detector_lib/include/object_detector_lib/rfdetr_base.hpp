#pragma once

#include <NvInfer.h>
#include <cuda_runtime.h>

#include <filesystem>
#include <string>
#include <vector>

#include "object_detector_lib/detection.hpp"
#include "object_detector_lib/detector_common.hpp"
#include "object_detector_lib/detector_impl.hpp"

namespace object_detector {

// What RfDetrDetector and RfDetrSegmenter share: an RF-DETR model as the rfdetr package exports it, one
// 1x3xRxR input (RGB, ImageNet-normalized), `dets` (1xQx4 boxes, normalized cx, cy, w, h), `labels`
// (1xQxC class logits) and, for the segmentation models, `masks` (1xQxMxM mask logits). The resolution,
// query count and class count are read from the engine. There's no NMS: each query scores every class
// independently, as in rfdetr's PostProcess.
class RfDetrBase : public DetectorImpl, public DetectorCommon {
   public:
    ~RfDetrBase() override;

    int numClasses() override;

    // Throws if a CUDA call failed, saying what it was doing.
    static void checkCuda(cudaError_t ret, const std::string &what);

   protected:
    RfDetrBase(std::filesystem::path filepath, float conf_threshold);

    // A (query, class) pair scoring above the confidence threshold.
    struct Candidate {
        float score;
        int query;
        int classId;
    };

    // Runs the model on `image` and returns its candidates, most confident first. Like rfdetr, it keeps at
    // most as many as the model has queries.
    std::vector<Candidate> infer(const cv::Mat &image);

    // The candidate's box in `image`'s pixels. Only valid after infer() on that image.
    cv::Rect bbox(const Candidate &candidate, const cv::Mat &image) const;

    // Whether the model has a `masks` output. A segmentation model's masks are bound even when only its
    // boxes are wanted: every output needs an address.
    bool hasMasks() const { return masks_ != nullptr; }

    int maskHeight_{ 0 }, maskWidth_{ 0 };
    float *masks_{ nullptr };  ///< Device buffer bound to `masks`, if the model has one

   private:
    // The shape of an I/O tensor, after the input shape is fixed. Throws unless it's FP32 with `rank` dims.
    nvinfer1::Dims tensorShape(const char *name, int rank);

    float confThreshold_;

    std::string inputName_;
    int inputHeight_, inputWidth_;
    int numQueries_, numClasses_;

    // Device buffers bound to the engine's other I/O tensors.
    float *input_{ nullptr }, *dets_{ nullptr }, *labels_{ nullptr };
    // Host copies of the outputs.
    std::vector<float> detsCopy_, labelsCopy_;
};

}  // namespace object_detector
