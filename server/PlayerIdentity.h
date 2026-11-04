#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "mc/network/NetworkIdentifier.h"

namespace lholo::sync {

struct PlayerIdentity {
    std::string xuid;
    bool isOperator{false};
    [[nodiscard]] bool authenticated() const { return !xuid.empty(); }
    [[nodiscard]] std::string actorKey() const;
};

class PlayerIdentityResolver {
public:
    static PlayerIdentityResolver& instance();
    [[nodiscard]] bool install();
    void uninstall() noexcept;
    [[nodiscard]] std::optional<PlayerIdentity> resolve(NetworkIdentifier const& source);
    void erase(NetworkIdentifier const& source);

private:
    PlayerIdentityResolver() = default;
    mutable std::mutex mMutex;
    std::unordered_map<NetworkIdentifier, PlayerIdentity> mCache;
};

} // namespace lholo::sync