#include "check-usize.h"
#include <asset/asset-helpers.h>
#include <asset/asset-db.h>
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>

namespace fty::asset {

struct Request : public pack::Node
{
    pack::String id       = FIELD("asset_id");
    pack::String parentId = FIELD("rack_id");
    pack::UInt32 usize    = FIELD("asset_size");
    pack::UInt32 location = FIELD("asset_position");

    using pack::Node::Node;
    META(Request, id, parentId, usize, location);
};

unsigned CheckUSize::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    if (m_request.type() != rest::Request::Type::Post) {
        throw rest::errors::MethodNotAllowed(m_request.typeStr());
    }

    std::string json = m_request.body();
    if (json.empty()) {
        throw rest::errors::BadInput("Payload is empty"_tr);
    }

    Request input;
    if (auto ret = pack::json::deserialize(json, input); !ret) {
        throw rest::errors::BadInput(ret.error());
    }

    if (input.parentId.empty()) {
        throw rest::errors::BadInput("Rack id couldn't be empty");
    }

    if (!input.usize.hasValue()) {
        throw rest::errors::BadInput("U-size is not set");
    }

    if (!input.location.hasValue()) {
        throw rest::errors::BadInput("Asset position is not set");
    }

    uint32_t id = 0;
    if (input.id.hasValue() && !input.id.empty()) {
        if (auto tmp = db::nameToAssetId(input.id)) {
            id = convert<uint32_t>(*tmp);
        } else {
            auditError("Wrong asset id {}, Error: {}"_tr, input.id.value(), tmp.error());
            throw rest::errors::RequestParamBad("asset_id", input.id.value(), "asset name");
        }
    }

    uint32_t parentId = 0;
    if (auto tmp = db::nameToAssetId(input.parentId)) {
        parentId = convert<uint32_t>(*tmp);
    } else {
        auditError("Wrong asset id {}, Error: {}"_tr, input.id.value(), tmp.error());
        throw rest::errors::RequestParamBad("rack_id", input.id.value(), "asset name");
    }

    auto ret = tryToPlaceAsset(id, parentId, input.usize, input.location);
    if (!ret) {
        throw rest::errors::Internal(ret.error());
    }

    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::CheckUSize)
