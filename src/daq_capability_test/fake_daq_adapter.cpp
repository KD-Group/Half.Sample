#include "daq_capability_test/fake_daq_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace daq_capability_test {
namespace {
template <typename T>
AdapterResult<T> successful()
{
    AdapterResult<T> result; result.success = true; return result;
}
template <typename T>
AdapterResult<T> failed(const std::string& code, const std::string& message, const std::string& stage,
                        const std::string& driver_error, bool unsupported)
{
    AdapterResult<T> result; result.code=code; result.message=message; result.stage=stage;
    result.driver_error=driver_error; result.unsupported=unsupported; return result;
}
}

FakeDaqAdapter::FakeDaqAdapter()
    : query_calls_(0), configure_calls_(0), acquire_calls_(0), trigger_calls_(0), stop_calls_(0), generated_acquisitions_(0) {}

void FakeDaqAdapter::enqueue_success(const AcquisitionData& data)
{
    AdapterResult<AcquisitionData> result = successful<AcquisitionData>();
    result.value = data; acquisitions_.push_back(result);
}

void FakeDaqAdapter::enqueue_failure(const std::string& code, const std::string& message,
                                     const std::string& stage, const std::string& driver_error)
{
    AdapterResult<AcquisitionData> result;
    result.code = code; result.message = message; result.stage = stage; result.driver_error = driver_error;
    acquisitions_.push_back(result);
}

void FakeDaqAdapter::enqueue_unsupported(const std::string& code, const std::string& message,
                                         const std::string& stage, const std::string& driver_error)
{
    AdapterResult<AcquisitionData> result;
    result.unsupported = true; result.code = code; result.message = message; result.stage = stage;
    result.driver_error = driver_error;
    acquisitions_.push_back(result);
}

void FakeDaqAdapter::enqueue_query_success(const CapabilityInfo& value)
{ AdapterResult<CapabilityInfo> r=successful<CapabilityInfo>(); r.value=value; queries_.push_back(r); }
void FakeDaqAdapter::enqueue_query_failure(const std::string& c,const std::string& m,const std::string& s,const std::string& e)
{ queries_.push_back(failed<CapabilityInfo>(c,m,s,e,false)); }
void FakeDaqAdapter::enqueue_query_unsupported(const std::string& c,const std::string& m,const std::string& s,const std::string& e)
{ queries_.push_back(failed<CapabilityInfo>(c,m,s,e,true)); }
void FakeDaqAdapter::enqueue_configure_success() { configurations_.push_back(successful<OperationInfo>()); }
void FakeDaqAdapter::enqueue_configure_failure(const std::string& c,const std::string& m,const std::string& s,const std::string& e)
{ configurations_.push_back(failed<OperationInfo>(c,m,s,e,false)); }
void FakeDaqAdapter::enqueue_configure_unsupported(const std::string& c,const std::string& m,const std::string& s,const std::string& e)
{ configurations_.push_back(failed<OperationInfo>(c,m,s,e,true)); }
void FakeDaqAdapter::enqueue_trigger_success() { triggers_.push_back(successful<OperationInfo>()); }
void FakeDaqAdapter::enqueue_trigger_failure(const std::string& c,const std::string& m,const std::string& s,const std::string& e)
{ triggers_.push_back(failed<OperationInfo>(c,m,s,e,false)); }
void FakeDaqAdapter::enqueue_trigger_unsupported(const std::string& c,const std::string& m,const std::string& s,const std::string& e)
{ triggers_.push_back(failed<OperationInfo>(c,m,s,e,true)); }

AdapterResult<CapabilityInfo> FakeDaqAdapter::query_capabilities(const std::string& device)
{
    ++query_calls_; queried_devices_.push_back(device);
    if (!queries_.empty()) { AdapterResult<CapabilityInfo> r=queries_.front(); queries_.pop_front(); return r; }
    AdapterResult<CapabilityInfo> r=successful<CapabilityInfo>(); r.value.max_channels=2;r.value.supports_trigger=true;
    r.value.min_sample_rate_hz=1;r.value.max_sample_rate_hz=2000000;r.value.max_points_per_channel=20000000;
    r.value.buffer_capacity=40000000;r.value.supported_ranges.push_back("-10V~10V");r.value.runtime_path="mock";r.value.runtime_version="1";return r;
}

AdapterResult<OperationInfo> FakeDaqAdapter::configure(const AcquisitionRequest& request)
{
    ++configure_calls_; configure_requests_.push_back(request);
    if (!configurations_.empty()) { AdapterResult<OperationInfo> r=configurations_.front(); configurations_.pop_front(); return r; }
    return successful<OperationInfo>();
}

AdapterResult<AcquisitionData> FakeDaqAdapter::acquire_once(const AcquisitionRequest& request)
{
    ++acquire_calls_; acquisition_requests_.push_back(request);
    if (acquisitions_.empty()) return generated(request);
    AdapterResult<AcquisitionData> result = acquisitions_.front(); acquisitions_.pop_front(); return result;
}

AdapterResult<OperationInfo> FakeDaqAdapter::configure_trigger(const TriggerRequest& request)
{
    ++trigger_calls_; trigger_requests_.push_back(request);
    if(request.mock_scenario=="trigger_timeout")return failed<OperationInfo>("TRIGGER_TIMEOUT","Mock trigger timed out","trigger","",false);
    if (!triggers_.empty()) { AdapterResult<OperationInfo> r=triggers_.front(); triggers_.pop_front(); return r; }
    return successful<OperationInfo>();
}

void FakeDaqAdapter::stop() noexcept { ++stop_calls_; }

AdapterResult<AcquisitionData> FakeDaqAdapter::generated(const AcquisitionRequest& request)
{
    ++generated_acquisitions_;
    const std::string scenario=request.mock_scenario.empty()?"success":request.mock_scenario;
    const unsigned int scenario_acquisition=scenario_acquisitions_[scenario+"|"+request.role]++;
    AdapterResult<AcquisitionData> out=successful<AcquisitionData>();
    AcquisitionData& data=out.value;data.actual_sample_rate_hz=request.sample_rate_hz;
    data.duration_seconds=request.points_per_channel/request.sample_rate_hz;
    data.trigger_wait_seconds=scenario=="long_trigger_wait"?10.0:0.0;data.wall_elapsed_seconds=data.duration_seconds+data.trigger_wait_seconds;
    if(scenario=="timeout")data.timed_out=true;
    if(scenario=="overrun")data.overrun=true;
    if(scenario=="cache_overflow")data.cache_overflow=true;
    unsigned int points=request.points_per_channel;
    if(scenario=="short_read"&&points) --points;
    data.channels.resize(request.channels.size());
    const double pi=3.14159265358979323846;
    int delay=1;
    if(!trigger_requests_.empty()&&trigger_requests_.back().delay_counts>0)delay=trigger_requests_.back().delay_counts;
    if(scenario=="delay_position_mismatch")delay+=10;
    for(unsigned int channel=0;channel<data.channels.size();++channel)for(unsigned int i=0;i<points;++i){
        const bool delay_to_start=!trigger_requests_.empty()&&trigger_requests_.back().action=="delay_to_start";
        const double duty=scenario=="duty_30"?0.3:0.5;
        const double edge_origin=delay_to_start&&trigger_requests_.back().edge=="falling"?duty:0.0;
        double phase=edge_origin+request.mock_signal_frequency_hz*(i/request.sample_rate_hz+(delay_to_start?delay*1e-6:0.0));
        if(scenario=="delay_repeat_drift"&&request.role!="delay_calibration"&&(scenario_acquisition%2))phase+=0.02;
        if(request.role=="phase_ordinary"&&scenario=="success") {
            const unsigned int offsets[]={4,8,3,9};
            phase+=offsets[scenario_acquisition%4]/10.0;
        }
        double value=std::sin(2*pi*phase);
        const bool calibration=request.role=="calibration";
        if((scenario=="overlap_mismatch"||scenario=="non_stationary")&&!calibration&&scenario_acquisition>0)
            value+=scenario=="overlap_mismatch"?1.0:0.4;
        if(scenario=="boundary_jump"&&!calibration&&i==0)value=3.0;
        if(channel==1) {
            if(scenario=="edge_missing"&&!calibration)value=(i%2)?3.0:1.0;
            else if(scenario=="edge_jitter"&&calibration) {
                const unsigned int period=static_cast<unsigned int>(request.sample_rate_hz/request.mock_signal_frequency_hz);
                const unsigned int half=period/2,jitter=(std::max)(1u,period/20);
                const bool high=(i>=1&&i<half)||(i>=period&&i<period+half)||
                    (i>=2*period+jitter&&i<2*period+jitter+half);
                value=high?5.0:-5.0;
            } else if(request.role=="phase_ordinary"||calibration) value=std::fmod(phase,1.0)<duty?5.0:-5.0;
            else value=std::fmod(phase,1.0)<duty?5.0:-5.0;
        }
        if(scenario=="reference_missing"&&channel==0)value=0.0;
        data.channels[channel].samples.push_back(value);
    }
    return out;
}

}  // namespace daq_capability_test
