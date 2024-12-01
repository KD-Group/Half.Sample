#include "sampler_factory.hpp"

#include <memory>
#include "mock_sampler.hpp"

#ifdef _WIN32

#include "real_sampler.hpp"

#endif

namespace Sampler {

    SamplerPtr SamplerFactory::get(const std::string &sampler_name) {
        SamplerPtr sampler;

        if (sampler_name == "mock_sampler") {
            sampler = std::make_shared<MockSampler>();
        }

#ifdef _WIN32
        if (sampler_name == "real_sampler") {
            sampler = std::make_shared<RealSampler>();
        }
#endif

        return sampler;
    }

} // namespace Sampler
