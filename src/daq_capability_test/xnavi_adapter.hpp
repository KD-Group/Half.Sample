#pragma once

#include "daq_capability_test/daq_adapter.hpp"
#include "daq_capability_test/range_normalization.hpp"

#include <memory>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace daq_capability_test {

inline bool select_xnavi_supported_voltage_range(const std::string& requested,
                                                  const std::vector<std::string>& supported,
                                                  size_t& selected_index)
{
    std::string normalized;
    for (size_t index = 0; index < requested.size(); ++index) {
        if (requested[index] != ' ' && requested[index] != '\t') normalized += requested[index];
    }
    const size_t separator = normalized.find('~');
    if (separator == std::string::npos || normalized.find('~', separator + 1) != std::string::npos ||
        separator == 0 || separator + 1 >= normalized.size() ||
        (normalized[separator - 1] != 'V' && normalized[separator - 1] != 'v') ||
        (normalized.back() != 'V' && normalized.back() != 'v')) return false;
    double low = 0.0, high = 0.0;
    if (!parse_voltage_number(normalized.substr(0, separator - 1), low) ||
        !parse_voltage_number(normalized.substr(separator + 1, normalized.size() - separator - 2), high) ||
        low >= high) return false;
    for (size_t index = 0; index < supported.size(); ++index) {
        if (equivalent_voltage_range(requested, supported[index])) {
            selected_index = index;
            return true;
        }
    }
    return false;
}

// ABI baseline paired with the vendored XNavi header snapshot. This does not
// claim compatibility or incompatibility for future header/runtime combinations.
constexpr int EXPECTED_DAQNAVI_TABLE_VERSION = 4;
constexpr int EXPECTED_DAQNAVI_TABLE_REVISION = 0;

struct XNaviFunctionTableView {
    int version;
    int revision;
    const void* global;
    const void* base;
    const void* ai;
};

inline AdapterResult<OperationInfo> validate_xnavi_function_table(const XNaviFunctionTableView* table)
{
    AdapterResult<OperationInfo> result;
    if (!table || table->version != EXPECTED_DAQNAVI_TABLE_VERSION ||
        table->revision != EXPECTED_DAQNAVI_TABLE_REVISION || !table->global || !table->base || !table->ai) {
        result.code = "HEADER_RUNTIME_INCOMPATIBLE";
        result.stage = "runtime_function_table";
        const std::string actual = table ? std::to_string(table->version) + "." + std::to_string(table->revision) : "null";
        result.message = "XNavi function table expected=4.0 actual=" + actual + "; Global/Base/Ai are required";
        result.evidence.push_back("expected_function_table_version=4.0");
        result.evidence.push_back("actual_function_table_version=" + actual);
        return result;
    }
    result.success = true;
    return result;
}

inline std::vector<std::vector<double> > deinterleave_xnavi_samples(
    const std::vector<double>& interleaved, unsigned int channel_count)
{
    if (!channel_count || interleaved.size() % channel_count) {
        throw std::invalid_argument("invalid interleaved shape");
    }
    std::vector<std::vector<double> > channels(channel_count);
    for (size_t point = 0; point < interleaved.size(); ++point) {
        channels[point % channel_count].push_back(interleaved[point]);
    }
    return channels;
}

inline CapabilityInfo xnavi_capability_from_scan_limit(unsigned int scan_limit)
{
    CapabilityInfo capability;
    capability.max_scan_count = scan_limit;
    capability.max_points_per_channel = scan_limit;
    return capability;
}

inline void write_xnavi_interleaved_chunk(const std::vector<double>& chunk, size_t global_offset,
                                           std::vector<std::vector<double> >& channels)
{
    if (channels.empty()) throw std::invalid_argument("channel count is zero");
    for (size_t index = 0; index < chunk.size(); ++index) {
        const size_t global = global_offset + index;
        const size_t channel = global % channels.size();
        const size_t sample = global / channels.size();
        if (sample >= channels[channel].size()) throw std::out_of_range("chunk exceeds destination");
        channels[channel][sample] = chunk[index];
    }
}

inline bool xnavi_demo_layout_matches_ranges(const std::vector<double>& values, double tolerance)
{
    if (values.size() < 16 || values.size() % 2) return false;
    bool even_above_one = false;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index % 2) {
            if (values[index] < -tolerance || values[index] > 1.0 + tolerance) return false;
        } else if (values[index] > 1.0 + tolerance) {
            even_above_one = true;
        }
    }
    return even_above_one;
}

inline AdapterResult<OperationInfo> validate_xnavi_config_readback(
    const OperationInfo& requested, const OperationInfo& actual)
{
    AdapterResult<OperationInfo> result;
    std::string field, requested_value, actual_value;
    if (requested.channel_start != actual.actual_channel_start) {
        field="channel_start"; requested_value=std::to_string(requested.channel_start); actual_value=std::to_string(actual.actual_channel_start);
    } else if (requested.channel_count != actual.actual_channel_count) {
        field="channel_count"; requested_value=std::to_string(requested.channel_count); actual_value=std::to_string(actual.actual_channel_count);
    } else if (requested.points_per_channel != actual.actual_samples) {
        field="samples"; requested_value=std::to_string(requested.points_per_channel); actual_value=std::to_string(actual.actual_samples);
    } else {
        const double rate_tolerance = (std::max)(0.01, std::fabs(requested.sample_rate_hz) * 1.0e-6);
        if (std::fabs(requested.sample_rate_hz - actual.sample_rate_hz) > rate_tolerance) {
            field="sample_rate_hz"; requested_value=std::to_string(requested.sample_rate_hz); actual_value=std::to_string(actual.sample_rate_hz);
        } else if (!equivalent_voltage_ranges(requested.actual_ranges,actual.actual_ranges)) {
            field="value_ranges"; requested_value="configured channel ranges"; actual_value="readback channel ranges";
        }
    }
    if (field.empty()) { result.success=true; result.value=actual; return result; }
    result.code="CONFIG_READBACK_MISMATCH"; result.stage="configure_readback";
    result.message=field + " requested=" + requested_value + " actual=" + actual_value;
    result.evidence.push_back("field="+field); result.evidence.push_back("requested="+requested_value); result.evidence.push_back("actual="+actual_value);
    return result;
}

inline AdapterResult<OperationInfo> classify_xnavi_missing_export(const std::string& export_name)
{
    AdapterResult<OperationInfo> result;
    const std::string core_export = "AdxDaqNaviLibInitialize";
    result.code = export_name == core_export
        ? "HEADER_RUNTIME_INCOMPATIBLE" : "ENTRY_POINT_MISSING";
    result.stage = "runtime_exports";
    result.message = "required XNavi runtime export is missing: " + export_name;
    return result;
}

inline std::vector<std::string> xnavi_required_runtime_exports()
{
    return {"AdxDaqNaviLibInitialize"};
}

class XNaviDaqAdapter : public DaqAdapter {
public:
    XNaviDaqAdapter();
    ~XNaviDaqAdapter() override;
    XNaviDaqAdapter(const XNaviDaqAdapter&) = delete;
    XNaviDaqAdapter& operator=(const XNaviDaqAdapter&) = delete;

    AdapterResult<CapabilityInfo> query_capabilities(const std::string& device) override;
    AdapterResult<OperationInfo> configure(const AcquisitionRequest& request) override;
    AdapterResult<AcquisitionData> acquire_once(const AcquisitionRequest& request) override;
    AdapterResult<OperationInfo> configure_trigger(const TriggerRequest& request) override;
    AdapterResult<AcquisitionData> verify_demo_interleaving(const std::string& device);
    void stop() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace daq_capability_test
