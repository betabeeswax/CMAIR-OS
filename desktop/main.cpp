#include "cmair/kernel.hpp"

#include <iostream>

int main() {
    cmair::Kernel kernel(cmair::Platform::Desktop);
    kernel.start();

    const auto& info = kernel.info();
    std::cout << "CMAIR-OS Desktop host " << info.version << "\n";
    std::cout << "Core state: "
              << (info.state == cmair::SystemState::Ready ? "ready" : "not ready")
              << "\n";
    std::cout << "This host target validates shared core logic; the full desktop shell is planned.\n";
    return 0;
}
