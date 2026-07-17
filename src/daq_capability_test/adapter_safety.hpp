#pragma once

#include "daq_capability_test/daq_adapter.hpp"

#include <mutex>

namespace daq_capability_test {

inline bool same_acquisition_request(const AcquisitionRequest& left, const AcquisitionRequest& right) {
    return left.device == right.device && left.channels == right.channels && left.value_range == right.value_range &&
           left.sample_rate_hz == right.sample_rate_hz && left.points_per_channel == right.points_per_channel &&
           left.timeout_seconds == right.timeout_seconds;
}

template <typename Result> class OnceInitializer {
  public:
    template <typename Function> const Result& get(Function function) {
        std::call_once(flag_, [&] { result_ = function(); });
        return result_;
    }

  private:
    std::once_flag flag_;
    Result result_{};
};

} // namespace daq_capability_test
