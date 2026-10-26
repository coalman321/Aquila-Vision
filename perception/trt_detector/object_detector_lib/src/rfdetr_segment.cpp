#include "object_detector_lib/rfdetr_segment.hpp"

#include <cuda_runtime.h>

#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace object_detector {

RfDetrSegmenter::RfDetrSegmenter(std::filesystem::path filepath, float conf_threshold) :
    RfDetrBase(filepath, conf_threshold) {
    if (!hasMasks()) {
        throw std::runtime_error("RF-DETR model " + filepath.string() +
                                 " has no masks output, so it can only detect, not segment");
    }
    maskCopy_.resize(maskHeight_ * maskWidth_);
}

std::vector<Detection> RfDetrSegmenter::runDetection(cv::Mat &image) {
    std::vector<Detection> detections;
    for (const Candidate &candidate : infer(image)) {
        // Only the kept queries' masks leave the GPU. A mask covers the whole input, so it's scaled to the
        // whole image; its logits are positive inside the object.
        checkCuda(cudaMemcpy(maskCopy_.data(), masks_ + candidate.query * maskCopy_.size(),
                             maskCopy_.size() * sizeof(float), cudaMemcpyDeviceToHost),
                  "memcpy from masks");
        cv::Mat logits(maskHeight_, maskWidth_, CV_32FC1, maskCopy_.data()), scaled, mask;
        cv::resize(logits, scaled, image.size(), 0, 0, cv::INTER_LINEAR);
        cv::cvtColor(scaled > 0, mask, cv::COLOR_GRAY2BGR);
        detections.emplace_back(candidate.classId, candidate.score, bbox(candidate, image), mask);
    }
    return detections;
}

}  // namespace object_detector
