#include "chassis_proxy_client/messages/msg_common.hpp"

namespace proxy_client {

SerializationError::SerializationError(const std::string& msg) { message += msg; }

const char* SerializationError::what() const throw() { return message.c_str(); }

BinIStream::BinIStream(const std::vector<uint8_t>& data) {
    iss.write(reinterpret_cast<char*>(const_cast<uint8_t*>(data.data())), data.size());
}

BinIStream& BinIStream::operator>>(double& v) {
    int64_t tmp;
    iss.read((char*)&tmp, sizeof(tmp));
    tmp = be64toh(tmp);
    v = tmp / FIXED_DIV;
    return *this;
}

BinIStream& BinIStream::operator>>(std::string& v){
    int16_t str_size;
    (*this) >> str_size;
    str_size = ntohs(str_size);
    char c;
    for(int16_t i = 0; i < str_size; i++){
        (*this) >> c;
        v += c;
    }
    return *this;
}

// Fixed-width integer deserializers (network byte order -> host)
BinIStream& BinIStream::operator>>(uint8_t& v) {
    iss.read((char*)&v, sizeof(v));
    return *this;
}

BinIStream& BinIStream::operator>>(int8_t& v) {
    iss.read((char*)&v, sizeof(v));
    return *this;
}

BinIStream& BinIStream::operator>>(uint16_t& v) {
    uint16_t tmp;
    iss.read((char*)&tmp, sizeof(tmp));
    tmp = ntohs(tmp);
    v = tmp;
    return *this;
}

BinIStream& BinIStream::operator>>(int16_t& v) {
    uint16_t tmp;
    iss.read((char*)&tmp, sizeof(tmp));
    tmp = ntohs(tmp);
    v = static_cast<int16_t>(tmp);
    return *this;
}

BinIStream& BinIStream::operator>>(uint32_t& v) {
    uint32_t tmp;
    iss.read((char*)&tmp, sizeof(tmp));
    tmp = ntohl(tmp);
    v = tmp;
    return *this;
}

BinIStream& BinIStream::operator>>(int32_t& v) {
    uint32_t tmp;
    iss.read((char*)&tmp, sizeof(tmp));
    tmp = ntohl(tmp);
    v = static_cast<int32_t>(tmp);
    return *this;
}

BinIStream& BinIStream::operator>>(uint64_t& v) {
    uint64_t tmp;
    iss.read((char*)&tmp, sizeof(tmp));
    tmp = be64toh(tmp);
    v = tmp;
    return *this;
}

BinIStream& BinIStream::operator>>(int64_t& v) {
    uint64_t tmp;
    iss.read((char*)&tmp, sizeof(tmp));
    tmp = be64toh(tmp);
    v = static_cast<int64_t>(tmp);
    return *this;
}

BinIStream& BinIStream::operator>>(TimeStamp& v) {
    TimeStamp tmp;
    (*this) >> tmp.sec;
    (*this) >> tmp.nanosec;
    v = tmp;
    return *this;
}

BinOStream::BinOStream() : oss() {}

BinOStream& BinOStream::operator<<(const double & v) {
    int64_t tmp = htobe64(static_cast<int64_t>(v * FIXED_DIV));
    oss.write((char*)&tmp, sizeof(tmp));
    return *this;
}

BinOStream& BinOStream::operator<<(const std::string& v){
    int16_t str_size = htons(v.size());
    oss.write((char*)&str_size, sizeof(int16_t));
    for(char c : v){
        oss.write((char*)&c, sizeof(char));
    }
    return *this;
}

// Fixed-width integer serializers (host -> network byte order)
BinOStream& BinOStream::operator<<(const uint8_t& v) {
    oss.write((char*)&v, sizeof(v));
    return *this;
}

BinOStream& BinOStream::operator<<(const int8_t& v) {
    oss.write((char*)&v, sizeof(v));
    return *this;
}

BinOStream& BinOStream::operator<<(const uint16_t& v) {
    uint16_t tmp = htons(v);
    oss.write((char*)&tmp, sizeof(tmp));
    return *this;
}

BinOStream& BinOStream::operator<<(const int16_t& v) {
    uint16_t tmp = htons(static_cast<uint16_t>(v));
    oss.write((char*)&tmp, sizeof(tmp));
    return *this;
}

BinOStream& BinOStream::operator<<(const uint32_t& v) {
    uint32_t tmp = htonl(v);
    oss.write((char*)&tmp, sizeof(tmp));
    return *this;
}

BinOStream& BinOStream::operator<<(const int32_t& v) {
    uint32_t tmp = htonl(static_cast<uint32_t>(v));
    oss.write((char*)&tmp, sizeof(tmp));
    return *this;
}

BinOStream& BinOStream::operator<<(const uint64_t& v) {
    uint64_t tmp = htobe64(v);
    oss.write((char*)&tmp, sizeof(tmp));
    return *this;
}

BinOStream& BinOStream::operator<<(const int64_t& v) {
    uint64_t tmp = htobe64(static_cast<uint64_t>(v));
    oss.write((char*)&tmp, sizeof(tmp));
    return *this;
}

BinOStream& BinOStream::operator<<(const TimeStamp& v) {
    (*this) << v.sec;
    (*this) << v.nanosec;
    return *this;
}

std::string BinOStream::str() const { return oss.str(); }

std::vector<uint8_t> BinOStream::serialize() const {
    std::vector<uint8_t> vec;
    for (uint8_t c : str()) {
        vec.push_back(c);
    }

    return vec;
}

}  // namespace proxy_client
