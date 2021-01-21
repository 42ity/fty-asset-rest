#include "read-byname.h"
#include "asset/asset-db.h"
#include "asset/json.h"
#include <fty/rest/component.h>

namespace fty::asset {

unsigned ReadByName::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    if (m_request.type() != rest::Request::Type::Get) {
        throw rest::errors::MethodNotAllowed(m_request.type());
    }

    auto name = m_request.queryArg<std::string>("name");
    if (!name) {
        throw rest::errors::RequestParamRequired("external_name");
    }

    auto it = db::selectAssetElementByName(*name, true);
    if (!it) {
        throw rest::errors::ElementNotFound(*name);
    }

    std::string jsonAsset = getJsonAsset(it->id);
    if (jsonAsset.empty()) {
        throw rest::errors::Internal("get json asset failed."_tr);
    }

    m_reply << jsonAsset << "\n\n";
    return HTTP_OK;
}

}

registerHandler(fty::asset::ReadByName)
