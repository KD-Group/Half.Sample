#ifndef INSTANT_ACQUISITION_HPP
#define INSTANT_ACQUISITION_HPP

#include "instant_ai.hpp"
#include "../result/sampling_result.hpp"

namespace Sampler {
namespace InstantAi {

class InstantAcquisitionPlatform {
  public:
    virtual ~InstantAcquisitionPlatform() {}
    virtual bool wait_until(double planned_seconds) = 0;
    virtual double now_seconds() = 0;
    virtual int read(double& voltage) = 0;
    virtual bool read_failed(int code) const = 0;
};

bool run_continuous_acquisition(const ContinuousSchedule& schedule, int target_cycles,
                                InstantAcquisitionPlatform& platform, Result::SamplingResult& result);

} // namespace InstantAi
} // namespace Sampler
#endif
