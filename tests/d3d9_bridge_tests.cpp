#include "../L4D2VR/d3d9_bridge_registry.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

struct RefCounted {
    std::atomic<unsigned> references{1};
    std::atomic<bool> alive{true};

    unsigned AddRef() {
        CHECK(alive.load());
        return ++references;
    }

    unsigned Release() {
        const unsigned remaining = --references;
        if (!remaining)
            alive = false;
        return remaining;
    }
};

using Registry = Portal2VRBridge::Registry<RefCounted, RefCounted>;

int main() {
    Registry registry;
    RefCounted* device = reinterpret_cast<RefCounted*>(1);
    RefCounted* bridge = reinterpret_cast<RefCounted*>(1);
    CHECK(!registry.AcquireSole(&device, &bridge));
    CHECK(!device && !bridge);

    RefCounted firstDevice, firstBridge, secondDevice, secondBridge;
    CHECK(registry.Register(&firstDevice, &firstBridge));
    CHECK(registry.AcquireSole(&device, &bridge));
    CHECK(device == &firstDevice && bridge == &firstBridge);
    CHECK(firstDevice.references == 2 && firstBridge.references == 2);
    bridge->Release();
    CHECK(registry.ReleaseDevice(device,
        [&] { return device->references == 1; },
        [&] { return device->Release(); }) == 1);
    CHECK(registry.AcquireSole(&device, &bridge));
    bridge->Release();
    CHECK(registry.ReleaseDevice(device,
        [&] { return device->references == 1; },
        [&] { return device->Release(); }) == 1);

    CHECK(registry.Register(&secondDevice, &secondBridge));
    device = reinterpret_cast<RefCounted*>(1);
    bridge = reinterpret_cast<RefCounted*>(1);
    CHECK(!registry.AcquireSole(&device, &bridge));
    CHECK(!device && !bridge);
    CHECK(firstDevice.references == 1 && secondDevice.references == 1);
    registry.Unregister(&secondDevice);
    CHECK(registry.AcquireSole(&device, &bridge));
    CHECK(device == &firstDevice && bridge == &firstBridge);
    bridge->Release();
    CHECK(registry.ReleaseDevice(device,
        [&] { return device->references == 1; },
        [&] { return device->Release(); }) == 1);

    std::promise<void> releaseEntered;
    std::promise<void> releaseMayFinish;
    auto finishRelease = releaseMayFinish.get_future();
    auto releasing = std::async(std::launch::async, [&] {
        return registry.ReleaseDevice(&firstDevice,
            [&] { return firstDevice.references == 1; },
            [&] {
                releaseEntered.set_value();
                finishRelease.wait();
                return firstDevice.Release();
            });
    });
    releaseEntered.get_future().wait();
    auto acquiring = std::async(std::launch::async, [&] {
        RefCounted* foundDevice = nullptr;
        RefCounted* foundBridge = nullptr;
        return registry.AcquireSole(&foundDevice, &foundBridge);
    });
    CHECK(acquiring.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    releaseMayFinish.set_value();
    CHECK(releasing.get() == 0);
    CHECK(!acquiring.get());
    CHECK(!firstDevice.alive && firstDevice.references == 0);

    std::cout << "D3D9 bridge registry tests passed\n";
}
