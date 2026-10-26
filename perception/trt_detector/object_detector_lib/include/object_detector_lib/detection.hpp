#pragma once

#include <opencv2/core.hpp>

namespace object_detector {

class Detection {
   public:
    Detection(int classId, float conf, cv::Rect boundingBox, cv::Mat mask);
    Detection(int classId, float conf, cv::Rect boundingBox);

    int classId()const;

    float confidence() const;

    cv::Rect bbox() const;

    cv::Mat mask();

   private:
    int class_id_;
    float conf_;
    bool is_seg_;
    cv::Rect bounding_box_;
    cv::Mat seg_mask_;
};

}  // namespace object_detector
