#pragma once

#include <cstdint>
#include <vector>
#include <functional>

namespace proxy_client {

class ProxySubscriptionBase {
   public:
    ProxySubscriptionBase() {}

    virtual void invokeCallback(const std::vector<uint8_t> & data) = 0;
};

template <typename T>
class ProxySubscription : public ProxySubscriptionBase {
   public:
    ProxySubscription(std::function<void(T result)> callback) : callback_(callback) {}

    void invokeCallback(const std::vector<uint8_t> & data) override {
        T msg = T::deserialize(data);
        callback_(msg);
    }

   private:
    std::function<void(T result)> callback_;
};

}  // namespace proxy_client
