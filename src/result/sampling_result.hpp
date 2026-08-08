#ifndef SAMPLING_RESULT_HPP
#define SAMPLING_RESULT_HPP

#include "../constant.hpp"
#include "../error/error.hpp"
#include "../estimate/estimate.hpp"
#include "../sampler/instant_ai.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <vector>

namespace Result {

struct SamplingProgress {
    std::atomic<long long> planned_milliseconds{0};
    std::atomic<long long> elapsed_milliseconds{0};
    std::atomic<int> completed_cycles{0};
    std::atomic<int> target_cycles{0};
    std::atomic<int> successful_reads{0};
    std::atomic<int> late_reads{0};
    std::atomic<bool> cancel_requested{false};
    std::mutex cancel_mutex;
    std::condition_variable cancel_condition;

    static long long to_milliseconds(double seconds) {
        if (std::isnan(seconds) || seconds <= 0.0) return 0;
        const double maximum = static_cast<double>(std::numeric_limits<long long>::max());
        if (!std::isfinite(seconds) || seconds >= maximum / 1000.0)
            return std::numeric_limits<long long>::max();
        return static_cast<long long>(seconds * 1000.0);
    }

    void reset(double planned_seconds, int cycles) {
        planned_milliseconds.store(to_milliseconds(planned_seconds));
        elapsed_milliseconds.store(0);
        completed_cycles.store(0);
        target_cycles.store(cycles < 0 ? 0 : cycles);
        successful_reads.store(0);
        late_reads.store(0);
        cancel_requested.store(false);
    }

    void request_cancel() {
        cancel_requested.store(true, std::memory_order_release);
        cancel_condition.notify_all();
    }

    void update_elapsed(double seconds) {
        const long long desired = to_milliseconds(seconds);
        long long current = elapsed_milliseconds.load(std::memory_order_relaxed);
        while (current < desired &&
               !elapsed_milliseconds.compare_exchange_weak(current, desired, std::memory_order_relaxed)) {
        }
    }

    bool wait_until_or_cancel(const std::chrono::steady_clock::time_point& deadline) {
        std::unique_lock<std::mutex> lock(cancel_mutex);
        cancel_condition.wait_until(lock, deadline, [&]() {
            return cancel_requested.load(std::memory_order_acquire);
        });
        return !cancel_requested.load(std::memory_order_acquire);
    }
};

struct SamplingResult {
    explicit SamplingResult(bool allocate_buffers = true)
        : totalSamplingBuffer(allocate_buffers ? static_cast<std::size_t>(Constant::MaxBufferSize) : 0u, 0.0),
          resultWave(allocate_buffers ? static_cast<std::size_t>(Constant::MaxBufferSize / 16) : 0u, 0.0) {}

    SamplingProgress progress;
    std::vector<double> totalSamplingBuffer;
    std::vector<double> resultWave;
    std::vector<Sampler::InstantAi::TimedWaveform> instant_ai_waveforms;
    Sampler::InstantAi::TimedReadings instant_ai_readings;
    int instant_ai_format_version{};
    int instant_ai_complete_waveforms{};
    double instant_ai_actual_duration_seconds{};
    int instant_ai_late_reads{};
    int instant_ai_interpolated_bins{};
    bool cancelled{};

    double maximum{}, minimum{};
    Estimate::EstimatedResult estimate;

    bool success{};
    std::atomic<bool> measuring{false};
    Error::Code error_code = Error::Code::SUCCESS;
};

} // namespace Result

#endif
