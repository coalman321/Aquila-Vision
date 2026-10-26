#pragma once

#include <cstdint>

namespace frc971::apriltag {

struct QuadCorners {
    float corners[4][2];
    bool reversed_border;
    uint32_t blob_index;
};

struct CameraMatrix {
    double fx;
    double cx;
    double fy;
    double cy;
};

struct DistCoeffs {
    double k1;
    double k2;
    double p1;
    double p2;
    double k3;
};

}  // namespace frc971::apriltag
