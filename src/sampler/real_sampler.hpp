#ifndef REAL_SAMPLER_HPP
#define REAL_SAMPLER_HPP

#include "sampler.hpp"

#include "Windows.h"
#include "bdaqctrl.h"
#include "../error/error.hpp"
#include <fstream>
using namespace std;
using namespace Automation::BDaq;

#define deviceDescription L"PCI-1714,BID#0"
#define check_code(code) if (BioFailed(code)) { result.error_code = static_cast<Error::Code>(code); return false; }

namespace Sampler {

class RealSampler: public Sampler {
    public:
    bool sample(const Config::SamplingConfig &config, Result::SamplingResult &result) override {
        ErrorCode code = Success;
        BufferedAiCtrl *bfdAiCtrl = AdxBufferedAiCtrlCreate();

        // connect device
        DeviceInformation devInfo(deviceDescription);
        code = bfdAiCtrl->setSelectedDevice(devInfo);
        check_code(code);

        // dynamic range setting
        AiChannelCollection* channels = bfdAiCtrl->getChannels();
        channels->getItem(0).setValueRange(V_Neg5To5);

        // sampling rate setting
        ConvertClock * convertClock = bfdAiCtrl->getConvertClock();
        convertClock->setRate(config.sampling_frequency);

        // sampling data
        ScanChannel* scanChannel = bfdAiCtrl->getScanChannel();
        code = scanChannel->setChannelStart(0);
        check_code(code);
        code = scanChannel->setChannelCount(1);
        check_code(code);
        code = scanChannel->setSamples(config.sampling_length_per_sample);
        check_code(code);
        code = bfdAiCtrl->Prepare();
        check_code(code);
        for (int i = 0; i < config.sampling_time; ++i) {
            code = bfdAiCtrl->RunOnce();
            check_code(code);
            code = bfdAiCtrl->GetData(config.sampling_length_per_sample, result.totalSamplingBuffer.data() + config.sampling_length_per_sample * i);
            check_code(code);
        }

        bfdAiCtrl->Dispose();

        dump_origin_data(config, result);

        return true;
    }

    double get_value(const std::string &key) override {
        return 0.0;
    }

    bool set_value(const std::string &key, const double value) override {
        return true;
    }

    std::string name() override {
        return "real_sampler";
    }
};

} // namespace Sampler

#endif