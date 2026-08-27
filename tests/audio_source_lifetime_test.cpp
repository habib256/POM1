#include "AudioDevice.h"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

class BlockingSource final : public AudioSource {
public:
    void fillAudioBuffer(float* output, int frameCount) override
    {
        entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (int i = 0; i < frameCount; ++i) output[i] = 0.25f;
    }

    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
};

} // namespace

int main()
{
    AudioDevice audio(/*initializeHardware=*/false);
    auto* source = new BlockingSource;
    audio.addSource(source);

    std::array<float, 64> output{};
    std::thread callback([&] {
        audio.mixSources(output.data(), static_cast<int>(output.size()));
    });
    while (!source->entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    std::atomic<bool> removeReturned{false};
    std::thread producer([&] {
        audio.removeSource(source);
        removeReturned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!removeReturned.load(std::memory_order_acquire));

    source->release.store(true, std::memory_order_release);
    callback.join();
    producer.join();
    assert(removeReturned.load(std::memory_order_acquire));
    delete source; // Safe immediately after the lifetime fence returned.

    output.fill(1.0f);
    audio.mixSources(output.data(), static_cast<int>(output.size()));
    for (float sample : output) assert(sample == 0.0f);

    std::puts("audio_source_lifetime_smoke: OK");
    return 0;
}
