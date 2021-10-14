/*  =========================================================================
    Copyright (C) 2021 Eaton.

    This software is confidential and licensed under Eaton Proprietary License
    (EPL or EULA).
    This software is not authorized to be used, duplicated or disclosed to
    anyone without the prior written permission of Eaton.
    Limitations, restrictions and exclusions of the Eaton applicable standard
    terms and conditions, such as its EPL and EULA, apply.
    =========================================================================
*/

#include "message.h"
#include <fty_common_messagebus_message.h>

namespace fty::asset {

// ===========================================================================================================

template <typename T>
struct identify
{
    using type = T;
};

template <typename K, typename V>
static V value(const std::map<K, V>& map, const typename identify<K>::type& key, const typename identify<V>::type& def = {})
{
    auto it = map.find(key);
    if (it != map.end()) {
        return it->second;
    }
    return def;
}

// ===========================================================================================================

Message::Message(const messagebus::Message& msg)
    : pack::Node::Node()
{
    meta.to            = value(msg.metaData(), messagebus::Message::TO);
    meta.from          = value(msg.metaData(), messagebus::Message::FROM);
    meta.replyTo       = value(msg.metaData(), messagebus::Message::REPLY_TO);
    meta.subject       = value(msg.metaData(), messagebus::Message::SUBJECT);
    meta.timeout       = value(msg.metaData(), messagebus::Message::TIMEOUT);
    meta.correlationId = value(msg.metaData(), messagebus::Message::CORRELATION_ID);

    meta.status.fromString(value(msg.metaData(), messagebus::Message::STATUS, "ok"));

    setData(msg.userData());
}

messagebus::Message Message::toMessageBus() const
{
    messagebus::Message msg;

    msg.metaData()[messagebus::Message::TO]             = meta.to;
    msg.metaData()[messagebus::Message::FROM]           = meta.from;
    msg.metaData()[messagebus::Message::REPLY_TO]       = meta.replyTo;
    msg.metaData()[messagebus::Message::SUBJECT]        = meta.subject;
    msg.metaData()[messagebus::Message::TIMEOUT]        = meta.timeout;
    msg.metaData()[messagebus::Message::CORRELATION_ID] = meta.correlationId;
    msg.metaData()[messagebus::Message::STATUS]         = meta.status.asString();

    for (const auto& el : userData) {
        msg.userData().emplace_back(el);
    }

    return msg;
}

void Message::setData(const std::string& data)
{
    userData.clear();
    userData.append(data);
}

void Message::setData(const std::list<std::string>& data)
{
    userData.clear();
    for (const auto& str : data) {
        userData.append(str);
    }
}

// ===========================================================================================================

} // namespace fty::asset
