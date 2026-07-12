#include "daq_capability_test/legacy_adapter.hpp"
#include <iostream>
#include <string>
#include <cmath>

namespace {
daq_capability_test::AdapterResult<daq_capability_test::AcquisitionData> acquire(
    const std::string& device, const std::string& range, const std::vector<int>& channels,
    unsigned int points, double rate, double timeout)
{
    daq_capability_test::LegacyDaqAdapter adapter;
    daq_capability_test::AcquisitionRequest request;
    request.device=device; request.channels=channels; request.value_range=range;
    request.sample_rate_hz=rate; request.points_per_channel=points; request.timeout_seconds=timeout;
    return adapter.acquire_once(request);
}

void print_sequence(const char* name, const std::vector<double>& values)
{
    std::cout << name << '=';
    for(size_t i=0;i<values.size() && i<16;++i) std::cout << (i?",":"") << values[i];
    std::cout << std::endl;
}
}

int main(int argc, char** argv)
{
    const std::string device = argc > 1 ? argv[1] : "__legacy_runtime_smoke_no_device__";
    daq_capability_test::LegacyDaqAdapter adapter;
    const auto result = adapter.query_capabilities(device);
    std::cout << "code=" << result.code << " stage=" << result.stage
              << " driver_error=" << result.driver_error << std::endl;
    if (!result.success) return 2;
    std::cout << "runtime_path=" << result.value.runtime_path
              << " runtime_version=" << result.value.runtime_version
              << " max_points_per_channel=" << result.value.max_points_per_channel << std::endl;
    if (argc < 3) return 0;
    const std::string mode=argv[2];
    const std::string range=result.value.supported_ranges.empty()?"V_Neg10To10":result.value.supported_ranges[0];
    if(mode=="validate-layout") {
        const auto verified=adapter.verify_demo_interleaving(device);
        std::cout<<verified.code<<" layout="<<verified.value.layout;
        for(const auto& evidence:verified.evidence) std::cout<<" evidence="<<evidence;
        std::cout<<std::endl;
        return verified.success?0:6;
    }
    if(mode=="timeout"||mode=="timeout-recover") {
        daq_capability_test::AcquisitionRequest timeout_request; timeout_request.device=device; timeout_request.channels={0,1}; timeout_request.value_range=range; timeout_request.points_per_channel=65535; timeout_request.sample_rate_hz=1000.0; timeout_request.timeout_seconds=0.001;
        const auto timed=adapter.acquire_once(timeout_request);
        std::cout<<"code="<<timed.code<<" stage="<<timed.stage<<" timed_out="<<(timed.value.timed_out?"true":"false")<<std::endl;
        if(timed.code!="TIMEOUT"||!timed.value.timed_out)return 7;
        if(mode=="timeout")return 6;
        timeout_request.points_per_channel=32; timeout_request.timeout_seconds=5.0;
        const auto recovered=adapter.acquire_once(timeout_request);
        std::cout<<"recovered="<<(recovered.success?"true":"false")<<std::endl;
        return recovered.success?0:9;
    }
    if(mode!="acquire") { std::cout<<"unknown mode"<<std::endl; return 8; }
    daq_capability_test::AcquisitionRequest request;
    request.device=device; request.channels={0,1};
    request.value_range=range;
    request.sample_rate_hz=1000.0; request.points_per_channel=32; request.timeout_seconds=5.0;
    const auto configured=adapter.configure(request);
    if(!configured.success){ std::cout<<"code="<<configured.code<<" stage="<<configured.stage<<" message="<<configured.message<<std::endl; return 3; }
    const auto acquired=adapter.acquire_once(request);
    std::cout<<"code="<<acquired.code<<" stage="<<acquired.stage;
    if(acquired.success) std::cout<<" channels="<<acquired.value.channels.size()
        <<" layout_unverified="<<(acquired.value.layout_unverified?"true":"false")<<" layout="<<acquired.value.layout;
    std::cout<<std::endl;
    return acquired.success ? 0 : 4;
}
