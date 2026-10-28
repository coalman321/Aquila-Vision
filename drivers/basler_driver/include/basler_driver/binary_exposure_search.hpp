#pragma once

namespace basler_driver {

// Bisects the exposure time towards a target mean brightness, for targets outside the range the
// camera's own auto functions can reach (AutoTargetBrightness covers roughly 50..205 of 255).
class BinaryExposureSearch {
   public:
    BinaryExposureSearch(float target_brightness, float left_limit, float right_limit);

    // Narrows the search interval from the brightness the last exposure produced. False when the
    // search has stalled (the same exposure proposed three times).
    bool update(float current_brightness, float current_exposure);

    float newExposure() const { return new_exposure_; }
    void limitReached(bool reached) { limit_reached_ = reached; }
    bool isLimitReached() const { return limit_reached_; }

   private:
    float target_brightness_;
    float left_limit_;
    float right_limit_;
    float new_exposure_;
    bool is_initial_setting_ = true;
    bool limit_reached_ = false;
    int unchanged_exposure_counter_ = 0;
};

}  // namespace basler_driver
