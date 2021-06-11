#include "actions-get.h"
#include "cxxtools/jsonserializer.h"
#include <fty/rest/component.h>
#include <fty_common_asset_types.h>
#include <fty_common_dto.h>
#include <fty_common_messagebus.h>
#include <fty_common_mlm_utils.h>

namespace fty::asset {

unsigned ActionsGet::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    Expected<std::string> id = m_request.queryArg<std::string>("id");
    if (!id) {
        throw rest::errors::RequestParamRequired("id");
    }
    if (!persist::is_ok_name(id->c_str())) {
        throw rest::errors::RequestParamBad("id", *id, "valid asset name"_tr);
    }

    auto msgbus = std::unique_ptr<messagebus::MessageBus>(
        messagebus::MlmMessageBus(MLM_ENDPOINT, messagebus::getClientId("tntnet")));
    msgbus->connect();

    dto::commands::GetCommandsQueryDto queryDto;
    queryDto.asset = *id;

    messagebus::Message msgRequest;
    msgRequest.metaData()[messagebus::Message::CORRELATION_ID] = messagebus::generateUuid();
    msgRequest.metaData()[messagebus::Message::TO]             = "fty-nut-command";
    msgRequest.metaData()[messagebus::Message::SUBJECT]        = "GetCommands";
    msgRequest.userData() << queryDto;
    auto msgReply = msgbus->request("ETN.Q.IPMCORE.POWERACTION", msgRequest, 10);

    if (msgReply.metaData()[messagebus::Message::STATUS] != "ok") {
        logError("Request to fty-nut-command failed.");
        throw rest::errors::PreconditionFailed("Request to fty-nut-command failed."_tr);
    }

    dto::commands::GetCommandsReplyDto replyDto;
    msgReply.userData() >> replyDto;

    cxxtools::SerializationInfo replySi;
    replySi.setCategory(cxxtools::SerializationInfo::Category::Array);

    for (const auto& command : replyDto) {
        cxxtools::SerializationInfo commandSi;
        commandSi.setCategory(cxxtools::SerializationInfo::Category::Object);
        commandSi.addValue("command", command.command);
        commandSi.addValue("description", command.description);

        cxxtools::SerializationInfo targetsSi;
        targetsSi.setName("targets");
        if (!command.targets.empty()) {
            targetsSi.setCategory(cxxtools::SerializationInfo::Category::Array);
            for (const auto& target : command.targets) {
                targetsSi.addMember("") <<= target;
            }
        }

        commandSi.addMember("targets") <<= targetsSi;
        replySi.addMember("") <<= commandSi;
    }

    // Serialize reply.
    std::stringstream        ss;
    cxxtools::JsonSerializer serializer(ss);
    serializer.serialize(replySi).finish();
    m_reply << ss.str();

    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::ActionsGet)
