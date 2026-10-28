#include "chassis_proxy_client/messages/odom/proxy_odom.hpp"

namespace proxy_client
{
    
    std::vector<uint8_t> ProxyOdomMsg::serialize(const ProxyOdomMsg & msg){
        std::vector<uint8_t> buffer;


        return buffer;

    }

    ProxyOdomMsg ProxyOdomMsg::deserialize(const std::vector<uint8_t> & data){
        ProxyOdomMsg msg;

        return msg;
    }


} // namespace proxy_client
