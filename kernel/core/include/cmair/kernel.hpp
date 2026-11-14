#pragma once

#include <cstdint>
#include <string_view>

namespace cmair {

enum class Platform : std::uint8_t {
    Unknown,
    CardputerAdv,
    Desktop
};

enum class SystemState : std::uint8_t {
    Cold,
    Starting,
    Ready,
    Recovery,
    ShuttingDown
};

struct SystemInfo {
    Platform platform{Platform::Unknown};
    SystemState state{SystemState::Cold};
    std::string_view version{"0.1.0-dev"};
};

class Kernel {
public:
    explicit Kernel(Platform platform) noexcept;
    void start() noexcept;
    void enter_recovery() noexcept;
    const SystemInfo& info() const noexcept;

private:
    SystemInfo info_{};
};

} // namespace cmair
