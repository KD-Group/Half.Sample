#include "daq_capability_test/legacy_adapter.hpp"
#include "daq_capability_test/trigger_mapping.hpp"
#include "daq_capability_test/adapter_safety.hpp"
#include <atomic>
#include <thread>

#include <cassert>
#include <vector>

void test_legacy_adapter() {
    daq_capability_test::AcquisitionData timing;
    daq_capability_test::map_acquisition_timing(timing, 300, 1000, 4.3);
    assert(timing.duration_seconds == 0.3 && timing.trigger_wait_seconds > 3.99 && timing.wall_elapsed_seconds == 4.3);
    size_t selected_range = 99;
    const std::vector<std::string> supported_ranges{"+/- 10 V", "0 ~ 10 V"};
    assert(daq_capability_test::select_legacy_supported_voltage_range("-10V~10V", supported_ranges, selected_range));
    assert(selected_range == 0);
    assert(!daq_capability_test::select_legacy_supported_voltage_range("-5V~5V", supported_ranges, selected_range));
    assert(!daq_capability_test::select_legacy_supported_voltage_range("5V", supported_ranges, selected_range));
    assert(!daq_capability_test::select_legacy_supported_voltage_range("-10~10", supported_ranges, selected_range));

    daq_capability_test::AcquisitionRequest saved, changed;
    saved.device = "Demo";
    saved.channels = {0, 1};
    saved.value_range = "range";
    saved.sample_rate_hz = 1;
    saved.points_per_channel = 2;
    saved.timeout_seconds = 3;
    changed = saved;
    assert(daq_capability_test::same_acquisition_request(saved, changed));
    changed.timeout_seconds = 4;
    assert(!daq_capability_test::same_acquisition_request(saved, changed));
    daq_capability_test::OnceInitializer<int> once;
    std::atomic<int> calls(0);
    std::thread a([&] {
        once.get([&] {
            ++calls;
            return 7;
        });
    });
    std::thread b([&] {
        once.get([&] {
            ++calls;
            return 7;
        });
    });
    a.join();
    b.join();
    assert(calls == 1);
    std::string mapped;
    assert(daq_capability_test::map_trigger_edge("rising", mapped) && mapped == "RisingEdge");
    assert(daq_capability_test::map_trigger_edge("falling", mapped) && mapped == "FallingEdge");
    assert(daq_capability_test::map_trigger_action("none", mapped) && mapped == "ActionNone");
    assert(daq_capability_test::map_trigger_action("delay_to_start", mapped) && mapped == "DelayToStart");
    assert(daq_capability_test::map_trigger_action("delay_to_stop", mapped) && mapped == "DelayToStop");
    assert(daq_capability_test::map_trigger_source("external_analog", mapped) && mapped == "SigExtAnaTrigger");
    assert(daq_capability_test::map_trigger_source("external", mapped) && mapped == "SigExtAnaTrigger");
    assert(daq_capability_test::map_trigger_source("external_digital_0", mapped) && mapped == "SigExtDigTrigger0");
    assert(!daq_capability_test::map_trigger_edge("arbitrary", mapped));
    assert(!daq_capability_test::map_trigger_source("SigAi42", mapped));
    const std::vector<double> input{10.0, 20.0, 11.0, 21.0, 12.0, 22.0};
    const std::vector<std::vector<double>> output = daq_capability_test::deinterleave_legacy_samples(input, 2);
    assert((output[0] == std::vector<double>{10.0, 11.0, 12.0}));
    assert((output[1] == std::vector<double>{20.0, 21.0, 22.0}));

    const daq_capability_test::CapabilityInfo capability =
        daq_capability_test::legacy_capability_from_scan_limit(16000000);
    assert(capability.max_scan_count == 16000000);
    assert(capability.max_points_per_channel == 16000000);

    daq_capability_test::OperationInfo requested;
    requested.channel_start = 0;
    requested.channel_count = 2;
    requested.points_per_channel = 64;
    requested.sample_rate_hz = 1000.0;
    requested.actual_ranges = {"V_Neg10To10", "V_Neg10To10"};
    daq_capability_test::OperationInfo actual = requested;
    actual.actual_channel_start = 0;
    actual.actual_channel_count = 2;
    actual.actual_samples = 64;
    actual.sample_rate_hz = 999.9995;
    assert(daq_capability_test::validate_legacy_config_readback(requested, actual).success);
    actual.actual_ranges = {"+/- 10 V", "-10V~10V"};
    assert(daq_capability_test::validate_legacy_config_readback(requested, actual).success);
    actual.actual_ranges = {"+/- 5 V", "-10V~10V"};
    assert(!daq_capability_test::validate_legacy_config_readback(requested, actual).success);
    actual.actual_ranges = requested.actual_ranges;
    actual.actual_samples = 63;
    const auto mismatch = daq_capability_test::validate_legacy_config_readback(requested, actual);
    assert(!mismatch.success);
    assert(mismatch.code == "CONFIG_READBACK_MISMATCH");
    assert(mismatch.stage == "configure_readback");
    assert(mismatch.message.find("samples") != std::string::npos);

    const auto incompatible = daq_capability_test::classify_legacy_missing_export("AdxBufferedAiCtrlCreate");
    assert(incompatible.code == "HEADER_RUNTIME_INCOMPATIBLE");
    assert(incompatible.stage == "runtime_exports");
    assert(incompatible.message.find("AdxBufferedAiCtrlCreate") != std::string::npos);
    const auto missing_helper = daq_capability_test::classify_legacy_missing_export("AdxEnumToString");
    assert(missing_helper.code == "ENTRY_POINT_MISSING");
    assert(missing_helper.stage == "runtime_exports");
    assert(missing_helper.message.find("AdxEnumToString") != std::string::npos);

    std::vector<std::vector<double>> chunked(2, std::vector<double>(3));
    daq_capability_test::write_legacy_interleaved_chunk({10.0, 20.0, 11.0}, 0, chunked);
    daq_capability_test::write_legacy_interleaved_chunk({21.0, 12.0, 22.0}, 3, chunked);
    assert((chunked[0] == std::vector<double>{10.0, 11.0, 12.0}));
    assert((chunked[1] == std::vector<double>{20.0, 21.0, 22.0}));

    const std::vector<std::string> required = daq_capability_test::legacy_required_runtime_exports();
    assert(required.size() == 3);
    assert(required[0] == "AdxBufferedAiCtrlCreate");
    assert(required[1] == "AdxEnumToString");
    assert(required[2] == "AdxStringToEnum");
    assert(daq_capability_test::classify_legacy_missing_export(required[1]).code == "ENTRY_POINT_MISSING");
    assert(daq_capability_test::classify_legacy_missing_export(required[2]).stage == "runtime_exports");

    const std::vector<std::string> instant_required = daq_capability_test::legacy_instant_ai_required_runtime_exports();
    assert(instant_required.size() == 1);
    assert(instant_required[0] == "AdxInstantAiCtrlCreate");
    assert(daq_capability_test::instant_ai_channels_are_contiguous({0}));
    assert(daq_capability_test::instant_ai_channels_are_contiguous({0, 1}));
    assert(!daq_capability_test::instant_ai_channels_are_contiguous({0, 2}));
    assert(!daq_capability_test::instant_ai_channels_are_contiguous({}));

    const std::vector<double> layout_values{2.0, 0.1, 3.0, 0.2, 4.0, 0.3, 5.0, 0.4,
                                            6.0, 0.5, 7.0, 0.6, 8.0, 0.7, 9.0, 0.8};
    assert(daq_capability_test::legacy_demo_layout_matches_ranges(layout_values, 1.0e-6));
    std::vector<double> bad_layout = layout_values;
    bad_layout[3] = 1.5;
    assert(!daq_capability_test::legacy_demo_layout_matches_ranges(bad_layout, 1.0e-6));
}
