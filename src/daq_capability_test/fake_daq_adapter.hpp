#pragma once

#include "daq_capability_test/daq_adapter.hpp"

#include <deque>
#include <map>

namespace daq_capability_test {

class FakeDaqAdapter : public DaqAdapter {
  public:
    FakeDaqAdapter();
    void enqueue_success(const AcquisitionData& data);
    void enqueue_failure(const std::string& code, const std::string& message = "", const std::string& stage = "acquire",
                         const std::string& driver_error = "");
    void enqueue_unsupported(const std::string& code, const std::string& message = "",
                             const std::string& stage = "acquire", const std::string& driver_error = "");
    void enqueue_query_success(const CapabilityInfo& value);
    void enqueue_query_failure(const std::string& code, const std::string& message = "",
                               const std::string& stage = "query", const std::string& driver_error = "");
    void enqueue_query_unsupported(const std::string& code, const std::string& message = "",
                                   const std::string& stage = "query", const std::string& driver_error = "");
    void enqueue_configure_success();
    void enqueue_configure_failure(const std::string& code, const std::string& message = "",
                                   const std::string& stage = "configure", const std::string& driver_error = "");
    void enqueue_configure_unsupported(const std::string& code, const std::string& message = "",
                                       const std::string& stage = "configure", const std::string& driver_error = "");
    void enqueue_trigger_success();
    void enqueue_trigger_failure(const std::string& code, const std::string& message = "",
                                 const std::string& stage = "trigger", const std::string& driver_error = "");
    void enqueue_trigger_unsupported(const std::string& code, const std::string& message = "",
                                     const std::string& stage = "trigger", const std::string& driver_error = "");

    AdapterResult<CapabilityInfo> query_capabilities(const std::string& device) override;
    AdapterResult<OperationInfo> configure(const AcquisitionRequest& request) override;
    AdapterResult<AcquisitionData> acquire_once(const AcquisitionRequest& request) override;
    AdapterResult<OperationInfo> configure_trigger(const TriggerRequest& request) override;
    void stop() noexcept override;

    int query_call_count() const { return query_calls_; }
    int configure_call_count() const { return configure_calls_; }
    int acquire_call_count() const { return acquire_calls_; }
    int trigger_call_count() const { return trigger_calls_; }
    int stop_call_count() const { return stop_calls_; }
    const std::vector<AcquisitionRequest>& acquisition_requests() const { return acquisition_requests_; }
    const std::vector<std::string>& queried_devices() const { return queried_devices_; }
    const std::vector<AcquisitionRequest>& configure_requests() const { return configure_requests_; }
    const std::vector<TriggerRequest>& trigger_requests() const { return trigger_requests_; }

  private:
    AdapterResult<AcquisitionData> generated(const AcquisitionRequest& request);
    std::deque<AdapterResult<AcquisitionData>> acquisitions_;
    std::deque<AdapterResult<CapabilityInfo>> queries_;
    std::deque<AdapterResult<OperationInfo>> configurations_, triggers_;
    std::vector<AcquisitionRequest> acquisition_requests_;
    std::vector<std::string> queried_devices_;
    std::vector<AcquisitionRequest> configure_requests_;
    std::vector<TriggerRequest> trigger_requests_;
    int query_calls_, configure_calls_, acquire_calls_, trigger_calls_, stop_calls_;
    unsigned int generated_acquisitions_;
    std::map<std::string, unsigned int> scenario_acquisitions_;
};

} // namespace daq_capability_test
