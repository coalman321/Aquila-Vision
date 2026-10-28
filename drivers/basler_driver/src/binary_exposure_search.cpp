#include "basler_driver/binary_exposure_search.hpp"

namespace basler_driver {

BinaryExposureSearch::BinaryExposureSearch(float target_brightness, float left_limit, float right_limit) :
    target_brightness_(target_brightness),
    left_limit_(left_limit),
    right_limit_(right_limit),
    new_exposure_((left_limit + right_limit) / 2.0f) {}

bool BinaryExposureSearch::update(float current_brightness, float current_exposure) {
    if (is_initial_setting_) {
        // The constructor already set the limits; the first exposure to try is their midpoint.
        is_initial_setting_ = false;
        return true;
    }
    if (current_brightness > target_brightness_) {
        right_limit_ = current_exposure;
    } else {
        left_limit_ = current_exposure;
    }
    new_exposure_ = (left_limit_ + right_limit_) / 2.0f;
    if (new_exposure_ == current_exposure) {
        ++unchanged_exposure_counter_;
    }
    return unchanged_exposure_counter_ <= 2;
}

}  // namespace basler_driver
