#pragma once

#include <string_view>

#include "Morbi/Network/NetworkTypes.hpp"

namespace Morbi::Network
{
    enum class RpcDirection : u8
    {
        ClientToServer = 0,
        ServerToClient = 1,
        Multicast = 2
    };

    enum class RpcAuthorityRule : u8
    {
        OwnerOnly = 0,
        ServerOnly = 1,
        AnyAuthenticatedPeer = 2
    };

    struct RpcDescriptor
    {
        u32 RpcId = 0;
        std::string_view Name;
        RpcDirection Direction = RpcDirection::ClientToServer;
        RpcAuthorityRule Authority = RpcAuthorityRule::OwnerOnly;
        NetDelivery Delivery = NetDelivery::ReliableOrdered;
        NetChannel Channel = NetChannel::GameplayEvents;
        u16 MaxPayloadBytes = 1024;
        u16 RateLimitPerSec = 30;
    };
}
