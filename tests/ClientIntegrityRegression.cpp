#include "Security/SecuritySystem.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// Never execute this function while its byte is changed in the test process.
__declspec(noinline) static int IntegrityFixture(int value) { return value * 13 + 7; }

int main() {
    try {
        volatile auto fixture = &IntegrityFixture;
        Check(fixture(3) == 46, "Fixture failed");
        auto& security = EngineSecurity::SecuritySystem::Get();
        uint32_t flags = 0;
        uint64_t baseline = 0;
        Check(security.PerformNetworkIntegrityCheck(flags, baseline) && flags == 0 && baseline != 0,
              "Initial code measurement failed");
        std::atomic<bool> valid{true};
        auto measure = [&] {
            for (int i = 0; i < 16; ++i) {
                uint32_t f = 0; uint64_t h = 0;
                if (!security.PerformNetworkIntegrityCheck(f, h) || f != 0 || h != baseline) valid = false;
            }
        };
        const auto start = std::chrono::steady_clock::now();
        std::thread worker(measure); measure(); worker.join();
        Check(valid, "Concurrent code measurements were inconsistent");
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        auto* code = reinterpret_cast<unsigned char*>(&IntegrityFixture);
        const auto original = *code;
        DWORD protection = 0, ignored = 0;
        Check(VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &protection) != 0, "Fixture protection failed");
        *code = original ^ 1;
        VirtualProtect(code, 1, protection, &ignored);
        const bool detected = !security.PerformNetworkIntegrityCheck(flags, baseline) && (flags & 8u);
        // Restore before assertions, even if detection failed.
        VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &ignored);
        *code = original;
        VirtualProtect(code, 1, protection, &ignored);
        FlushInstructionCache(GetCurrentProcess(), code, 1);
        Check(detected, "Code modification was not detected");
        Check(security.PerformNetworkIntegrityCheck(flags, baseline) && flags == 0,
              "Restored code measurement failed");
        std::cout << "PASS: real Windows code measurement, concurrent requests, code change detection/restoration; "
                  << elapsed / 32.0 << " ms/check\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
