#pragma once
#include "message.h"
#include <functional>
#include <memory>
#include <mutex>

// =====================================================================================================================

namespace messagebus {
class MessageBus;
class Message;
} // namespace messagebus

// =====================================================================================================================

namespace fty::asset {

/// Common message bus temporary wrapper
class MessageBus
{
public:
    static constexpr const char* endpoint = "ipc://@/malamute";

public:
    MessageBus();
    ~MessageBus();

    [[nodiscard]] Expected<void> init(const std::string& actorName);

    [[nodiscard]] Expected<Message> send(const std::string& queue, const Message& msg);
    [[nodiscard]] Expected<void>    publish(const std::string& queue, const Message& msg);
    [[nodiscard]] Expected<void>    reply(const std::string& queue, const Message& req, const Message& answ);
    [[nodiscard]] Expected<Message> receive(const std::string& queue);

    template <typename Func, typename Cls>
    [[nodiscard]] Expected<void> subscribe(const std::string& queue, Func&& fnc, Cls* cls)
    {
        return subscribe(queue, [f = std::move(fnc), c = cls](const messagebus::Message& msg) -> void {
            std::invoke(f, *c, Message(msg));
        });
    }

private:
    Expected<void> subscribe(const std::string& queue, std::function<void(const messagebus::Message&)>&& func);

private:
    std::unique_ptr<messagebus::MessageBus> m_bus;
    std::mutex                              m_mutex;
    std::string                             m_actorName;
};

} // namespace fty::asset
