#pragma once

#include "asset/asset-dto.h"
#include <fty/expected.h>
#include <pack/pack.h>

namespace fty::asset {

Expected<void> sendStreamNotification(const std::string& stream, const std::string& subject, const std::string& payload);

namespace notification::created {
    struct Topic
    {
        static constexpr const char* Full  = "FTY.T.ASSET.CREATED";
        static constexpr const char* Light = "FTY.T.ASSET_LIGHT.CREATED";
    };

    struct Subject
    {
        static constexpr const char* Full  = "CREATED";
        static constexpr const char* Light = "CREATED_LIGHT";
    };

    using PayloadFull  = Dto;
    using PayloadLight = pack::String;
} // namespace notification::created

namespace notification::updated {
    struct Topic
    {
        static constexpr const char* Full  = "FTY.T.ASSET.UPDATED";
        static constexpr const char* Light = "FTY.T.ASSET_LIGHT.UPDATED";
    };

    struct Subject
    {
        static constexpr const char* Full  = "UPDATED";
        static constexpr const char* Light = "UPDATED_LIGHT";
    };

    struct PayloadFull : public pack::Node
    {
        Dto before = FIELD("before");
        Dto after  = FIELD("after");

        using pack::Node::Node;

        META(PayloadFull, before, after);
    };
    using PayloadLight = pack::String;
} // namespace notification::updated

namespace notification::deleted {
    struct Topic
    {
        static constexpr const char* Full  = "FTY.T.ASSET.DELETED";
        static constexpr const char* Light = "FTY.T.ASSET_LIGHT.DELETED";
    };

    struct Subject
    {
        static constexpr const char* Full  = "DELETED";
        static constexpr const char* Light = "DELETED_LIGHT";
    };

    using PayloadFull  = Dto;
    using PayloadLight = pack::String;
} // namespace notification::deleted

} // namespace fty::asset
