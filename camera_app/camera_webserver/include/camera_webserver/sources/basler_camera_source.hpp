#pragma once

#include <rclcpp/logger.hpp>
#include <string>

#include "camera_webserver/sources/source_base.hpp"

namespace camera_web::sources {

class BaslerCameraSource : public CameraSource {
   public:
    BaslerCameraSource();

    std::set<VideoSourceConfig> listConnectedSources() override;
    bool isSourceConnected(const std::string& name) override;
    std::string getSourceTypeName() const override;

   private:
    rclcpp::Logger logger_;
};
}  // namespace camera_web::sources
