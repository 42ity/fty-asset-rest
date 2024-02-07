/*  ========================================================================================================================================
    Copyright (C) 2014 - 2020 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    ========================================================================================================================================
*/

#include "actions-post.h"
#include <asset/asset-db.h>
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>
#include <fty_commands_dto.h>
#include <fty_common_asset_types.h>
#include <fty_common_messagebus.h>
#include <fty_common_mlm_utils.h>
#include <fty_common_json.h>
#include <cxxtools/serializationinfo.h>

namespace fty::asset {

unsigned ActionsPost::run()
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

    auto item = db::nameToExtName(*id);
    if (!item) {
        throw rest::errors::Internal(item.error());
    }

    auto msgbus = std::unique_ptr<messagebus::MessageBus>(
        messagebus::MlmMessageBus(MLM_ENDPOINT, messagebus::getClientId("tntnet")));
    msgbus->connect();

    // Read json, transform to command list
    cxxtools::SerializationInfo            si;
    dto::commands::PerformCommandsQueryDto commandList;

    try {
        JSON::readFromString(m_request.body(), si);

        if (si.category() != cxxtools::SerializationInfo::Category::Array) {
            throw std::runtime_error("expected array of objects");
        }

        for (const auto& i : si) {
            dto::commands::Command command;
            command.asset = *id;

            if (i.category() != cxxtools::SerializationInfo::Category::Object) {
                throw std::runtime_error("expected array of objects");
            }
            if (!i.getMember("command", command.command)) {
                throw std::runtime_error("expected command key in object");
            }
            i.getMember("target", command.target);
            i.getMember("argument", command.argument);
            commandList.commands.push_back(command);
        }
    } catch (const std::exception& e) {
        logError("Error while parsing document: {}", e.what());
        auditError("Request CREATE asset_actions asset {} FAILED", *item);
        throw rest::errors::BadRequestDocument("Error while parsing document: {}"_tr.format(e.what()));
    }

    messagebus::Message msgRequest;
    msgRequest.metaData()[messagebus::Message::CORRELATION_ID] = messagebus::generateUuid();
    msgRequest.metaData()[messagebus::Message::TO]             = "fty-nut-command";
    msgRequest.metaData()[messagebus::Message::SUBJECT]        = "PerformCommands";
    msgRequest.userData() << commandList;
    auto msgReply = msgbus->request("ETN.Q.IPMCORE.POWERACTION", msgRequest, 10);

    if (msgReply.metaData()[messagebus::Message::STATUS] != "ok") {
        logError("Request to fty-nut-command failed.");
        auditError("Request CREATE asset_actions asset {} FAILED"_tr, *item);
        throw rest::errors::PreconditionFailed("Request to fty-nut-command failed."_tr);
    }

    auditInfo("Request CREATE asset_actions asset {} SUCCESS", *item);
    m_reply << "{}";

    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::ActionsPost)
