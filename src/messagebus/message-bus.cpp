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

#include "message-bus.h"
#include <fty_common_messagebus_exception.h>
#include <fty_common_messagebus_interface.h>
#include <fty_common_messagebus_message.h>
#include <fty_log.h>

namespace fty::asset {

MessageBus::MessageBus() = default;

Expected<void> MessageBus::init(const std::string& actorName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        m_bus = std::unique_ptr<messagebus::MessageBus>(messagebus::MlmMessageBus(endpoint, actorName));
        m_bus->connect();
        m_actorName = actorName;
        return {};
    } catch (std::exception& ex) {
        return fty::unexpected(ex.what());
    }
}

MessageBus::~MessageBus()
{
}

Expected<Message> MessageBus::send(const std::string& queue, const Message& msg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (msg.meta.correlationId.empty()) {
        msg.meta.correlationId = messagebus::generateUuid();
    }
    msg.meta.from = m_actorName;
    try {
        Message m(m_bus->request(queue, msg.toMessageBus(), 10000));
        if (m.meta.status == Message::Status::Error) {
            return unexpected(m.userData[0]);
        }
        return Expected<Message>(m);
    } catch (messagebus::MessageBusException& ex) {
        return unexpected(ex.what());
    }
}

Expected<void> MessageBus::publish(const std::string& queue, const Message& msg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    msg.meta.from = m_actorName;
    try {
        m_bus->publish(queue, msg.toMessageBus());
    } catch (messagebus::MessageBusException& ex) {
        return unexpected(ex.what());
    }
    return {};
}

Expected<void> MessageBus::reply(const std::string& queue, const Message& req, const Message& answ)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    answ.meta.correlationId = req.meta.correlationId;
    answ.meta.to            = req.meta.replyTo;
    answ.meta.from          = m_actorName;

    try {
        m_bus->sendReply(queue, answ.toMessageBus());
        return {};
    } catch (messagebus::MessageBusException& ex) {
        return unexpected(ex.what());
    }
}

Expected<Message> MessageBus::receive(const std::string& queue)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Message                     ret;
    try {
        m_bus->receive(queue, [&ret](const messagebus::Message& msg) {
            ret = Message(msg);
        });
        return Expected<Message>(ret);
    } catch (messagebus::MessageBusException& ex) {
        return unexpected(ex.what());
    }
}

Expected<void> MessageBus::subscribe(const std::string& queue, std::function<void(const messagebus::Message&)>&& func)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        m_bus->subscribe(queue, func);
        return {};
    } catch (messagebus::MessageBusException& ex) {
        return unexpected(ex.what());
    }
}

} // namespace fty::asset
