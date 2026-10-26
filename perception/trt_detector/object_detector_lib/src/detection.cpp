#include "object_detector_lib/detection.hpp"

namespace object_detector {

Detection::Detection(int classId, float conf, cv::Rect boundingBox, cv::Mat mask) :
    class_id_(classId), conf_(conf), bounding_box_(boundingBox), seg_mask_(mask) {
    is_seg_ = true;
}

Detection::Detection(int classId, float conf, cv::Rect boundingBox) :
    class_id_(classId), conf_(conf), bounding_box_(boundingBox) {
    is_seg_ = false;
}

int Detection::classId() const { return class_id_; }

float Detection::confidence() const { return conf_; }

cv::Rect Detection::bbox() const { return bounding_box_; }

cv::Mat Detection::mask() {
    if (!is_seg_) {
        throw std::runtime_error("Tried to get mask from non-segmented detection");
    }
    return seg_mask_;
}

}  // namespace object_detector
