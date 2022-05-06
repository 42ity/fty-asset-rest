#include "import.h"
#include <asset/asset-manager.h>
#include <asset/asset-configure-inform.h>
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>

#include <fty_log.h>
#include <fty_common.h>
#include <fty_common_mlm_utils.h>
#include <zmq.h>
#include <malamute.h>

namespace fty::asset {

struct Result : public pack::Node
{
    pack::Int32                        okLines = FIELD("imported_lines");
    pack::ObjectList<pack::StringList> errors  = FIELD("errors");

    using pack::Node::Node;
    META(Result, okLines, errors);
};

static void s_update_all()
{
    logInfo("import-csv update all");

    mlm_client_t* client = nullptr;
    std::string agentName{generateMlmClientId("web.import-csv")};

    do {
        client = mlm_client_new();
        if (!client)
            { logError("mlm_client_new () failed."); break; }
        int r = mlm_client_connect(client, MLM_ENDPOINT, 1000, agentName.c_str());
        if (r == -1)
            { logError("mlm_client_connect () failed."); break; }
        zmsg_t* msg = zmsg_new();
        r = mlm_client_sendto(client, AGENT_FTY_ASSET, "REPUBLISH", nullptr, 5000, &msg);
        zmsg_destroy(&msg);
        if (r != 0)
            { logError("sendto %s REPUBLISH failed.", AGENT_FTY_ASSET); break; }
        break;
    } while(0);

    mlm_client_destroy(&client);
}

unsigned RestImport::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    // HARDCODED limit: can't import things larger than 128K
    // this prevents DoS attacks against the box - can be raised if needed
    // don't forget internal processing is in UCS-32, therefore the
    // real memory requirements are ~640kB
    // Content size = body + something. So max size of body is about 125k
    if (m_request.contentSize() > 128 * 1024) {
        auditError("Request CREATE asset_import FAILED {}"_tr, "can't import things larger than 128K"_tr);
        throw rest::errors::ContentTooBig("128k");
    }

    if (auto part = m_request.multipart("assets")) {
        auto res = AssetManager::importCsv(*part, user.login());
        if (!res) {
            throw rest::errors::Internal(res.error());
        }
        Result result;
        for (const auto& [row, el] : *res) {
            if (el) {
                result.okLines = result.okLines + 1;
            } else {
                pack::StringList err;
                err.append(fty::convert<std::string>(row));
                err.append(el.error());
                result.errors.append(err);
            }
        }

        logInfo("import-csv, result.okLines: {}", result.okLines);
        if (result.okLines != 0) {
            s_update_all();
        }

        m_reply << *pack::json::serialize(result);
        return HTTP_OK;
    } else {
        auditError("Request CREATE asset_import FAILED {}"_tr, part.error());
        throw rest::errors::RequestParamRequired("file=assets");
    }
}

} // namespace fty::asset

registerHandler(fty::asset::RestImport)
