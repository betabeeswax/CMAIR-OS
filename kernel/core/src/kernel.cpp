#include "cmair/kernel.hpp"

namespace cmair {

Kernel::Kernel(Platform platform) noexcept {
    info_.platform = platform;
}

void Kernel::start() noexcept {
    info_.state = SystemState::Starting;
    // Platform services are initialized by the target-specific host/firmware.
    info_.state = SystemState::Ready;
}

void Kernel::enter_recovery() noexcept {
    info_.state = SystemState::Recovery;
}

const SystemInfo& Kernel::info() const noexcept {
    return info_;
}

} // namespace cmair
