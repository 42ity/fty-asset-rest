#include "notifications.h"
#include "message-bus.h"
#include "message.h"
#include <asset/asset-configure-inform.h>

namespace fty::asset {
Expected<void> sendStreamNotification(const std::string& stream, const std::string& subject, const std::string& payload)
{
    auto actor = generateMlmClientId("asset.notification");

    Message notification;
    notification.meta.from    = actor;
    notification.meta.subject = subject;
    notification.meta.status  = Message::Status::Ok;
    notification.userData.append(payload);

    MessageBus bus;
    if (auto ret = bus.init(actor); !ret) {
        return unexpected(ret.error());
    }

    if (auto ret = bus.publish(stream, notification); !ret) {
        return unexpected(ret.error());
    }

    return {};
}
} // namespace fty::asset
