#pragma once

#include <arpa/inet.h>
#include <endian.h>

#include <builtin_interfaces/msg/time.hpp>
#include <rclcpp/time.hpp>
#include <sstream>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#define FIXED_DIV 1.0e6

namespace proxy_client {

template <typename T>
struct Serializable {
    virtual std::vector<uint8_t> serialize() const = 0;
    virtual T deserialize(const std::vector<uint8_t>& data) = 0;
};

struct TimeStamp {
    int32_t sec{ 0 };
    int32_t nanosec{ 0 };

    TimeStamp() = default;

    TimeStamp(int32_t s, int32_t ns) : sec(s), nanosec(ns) {}

    double toSec() const { return static_cast<double>(sec) + static_cast<double>(nanosec) / 1.0e9; }

    TimeStamp offsetBy(double offset_sec) const {
        double sec_floor = std::floor(offset_sec);
        int64_t delta_sec = static_cast<int64_t>(sec_floor);
        double frac = offset_sec - sec_floor; // in [0,1)
        int64_t delta_nsec = static_cast<int64_t>(std::llround(frac * 1.0e9));

        int64_t new_sec = static_cast<int64_t>(sec) + delta_sec;
        int64_t new_nsec = static_cast<int64_t>(nanosec) + delta_nsec;

        const int64_t NSEC_PER_SEC = 1000000000LL;
        if (new_nsec >= NSEC_PER_SEC) {
            new_sec += new_nsec / NSEC_PER_SEC;
            new_nsec = new_nsec % NSEC_PER_SEC;
        } else if (new_nsec < 0) {
            int64_t borrow = (-new_nsec + NSEC_PER_SEC - 1) / NSEC_PER_SEC;
            new_sec -= borrow;
            new_nsec += borrow * NSEC_PER_SEC;
        }

        return TimeStamp(static_cast<int32_t>(new_sec), static_cast<int32_t>(new_nsec));
    }

    static TimeStamp fromRosTime(const builtin_interfaces::msg::Time& time) {
        return TimeStamp(time.sec, time.nanosec);
    }

    static TimeStamp fromRosTime(const rclcpp::Time& time) {
        return TimeStamp(time.seconds(), time.nanoseconds() % static_cast<int64_t>(1e9));
    }
};

class SerializationError : public std::exception {
   protected:
    std::string message = "Failed to deserialize type: ";

   public:
    SerializationError(const std::string& msg);

    // Override the what() method to return our message
    const char* what() const throw();
};

class BinIStream {
   public:
    BinIStream(const std::vector<uint8_t>& data);

    template <typename T, typename std::enable_if<std::is_fundamental<T>::value, bool>::type = true>
    BinIStream& operator>>(T& v) {
        iss.read((char*)&v, sizeof(T));
        return *this;
    }

    template <typename T,
              typename std::enable_if<std::is_fundamental<typename T::value_type>::value, bool>::type = true>
    BinIStream& operator>>(T& v) {
        iss.read((char*)v.data(), v.size() * sizeof(typename T::value_type));
        return *this;
    }

    BinIStream& operator>>(double& v);

    BinIStream& operator>>(std::string& v);

    // Fixed-width integer overloads use network byte order conversions
    BinIStream& operator>>(uint8_t& v);
    BinIStream& operator>>(int8_t& v);
    BinIStream& operator>>(uint16_t& v);
    BinIStream& operator>>(int16_t& v);
    BinIStream& operator>>(uint32_t& v);
    BinIStream& operator>>(int32_t& v);
    BinIStream& operator>>(uint64_t& v);
    BinIStream& operator>>(int64_t& v);

    // custom type overloads
    BinIStream& operator>>(TimeStamp& v);

   protected:
    std::stringstream iss;
};

class BinOStream {
   public:
    BinOStream();

    template <typename T, typename std::enable_if<std::is_fundamental<T>::value, bool>::type = true>
    BinOStream& operator<<(const T& v) {
        oss.write((char*)&v, sizeof(T));
        return *this;
    }

    template <typename T,
              typename std::enable_if<std::is_fundamental<typename T::value_type>::value, bool>::type = true>
    BinOStream& operator<<(const T& v) {
        oss.write((char*)v.data(), v.size() * sizeof(typename T::value_type));
        return *this;
    }

    BinOStream& operator<<(const double& v);

    BinOStream& operator<<(const std::string& v);

    // Fixed-width integer overloads use network byte order conversions
    BinOStream& operator<<(const uint8_t& v);
    BinOStream& operator<<(const int8_t& v);
    BinOStream& operator<<(const uint16_t& v);
    BinOStream& operator<<(const int16_t& v);
    BinOStream& operator<<(const uint32_t& v);
    BinOStream& operator<<(const int32_t& v);
    BinOStream& operator<<(const uint64_t& v);
    BinOStream& operator<<(const int64_t& v);

    // Custom type overloads
    BinOStream& operator<<(const TimeStamp& v);

    std::vector<uint8_t> serialize() const;

   protected:
    std::string str() const;

    std::ostringstream oss;
};

}  // namespace proxy_client
