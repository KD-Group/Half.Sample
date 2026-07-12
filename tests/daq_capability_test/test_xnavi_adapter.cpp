#include "daq_capability_test/xnavi_adapter.hpp"
#include "daq_capability_test/trigger_mapping.hpp"
#include "daq_capability_test/adapter_safety.hpp"

#include <cassert>
#include <vector>

void test_xnavi_adapter()
{
    daq_capability_test::AcquisitionData timing;daq_capability_test::map_acquisition_timing(timing,300,1000,4.3);assert(timing.duration_seconds==0.3&&timing.trigger_wait_seconds>3.99&&timing.wall_elapsed_seconds==4.3);
    size_t selected_range = 99;
    const std::vector<std::string> supported_ranges{"+/- 10 V", "0 ~ 10 V"};
    assert(daq_capability_test::select_xnavi_supported_voltage_range(
        "-10V~10V", supported_ranges, selected_range));
    assert(selected_range == 0);
    assert(!daq_capability_test::select_xnavi_supported_voltage_range(
        "-5V~5V", supported_ranges, selected_range));
    assert(!daq_capability_test::select_xnavi_supported_voltage_range(
        "5V", supported_ranges, selected_range));
    assert(!daq_capability_test::select_xnavi_supported_voltage_range(
        "-10~10", supported_ranges, selected_range));

    daq_capability_test::AcquisitionRequest saved, changed;
    saved.device="Demo"; saved.channels={0,1}; saved.value_range="range"; saved.sample_rate_hz=1;
    saved.points_per_channel=2; saved.timeout_seconds=3; changed=saved;
    assert(daq_capability_test::same_acquisition_request(saved, changed));
    changed.channels={1,2}; assert(!daq_capability_test::same_acquisition_request(saved, changed));
    std::string mapped;
    assert(daq_capability_test::map_trigger_edge("rising", mapped) && mapped == "RisingEdge");
    assert(daq_capability_test::map_trigger_action("delay_to_start", mapped) && mapped == "DelayToStart");
    assert(daq_capability_test::map_trigger_source("external_digital_0", mapped) && mapped == "SigExtDigTrigger0");
    assert(daq_capability_test::map_trigger_source("external", mapped) && mapped == "SigExtAnaTrigger");
    assert(!daq_capability_test::map_trigger_action("start_sometime", mapped));

    daq_capability_test::XNaviFunctionTableView table = {};
    assert(!daq_capability_test::validate_xnavi_function_table(0).success);
    table.version = daq_capability_test::EXPECTED_DAQNAVI_TABLE_VERSION;
    table.revision = daq_capability_test::EXPECTED_DAQNAVI_TABLE_REVISION;
    table.global = &table; table.base = &table; table.ai = &table;
    assert(daq_capability_test::validate_xnavi_function_table(&table).success);
    table.version++;
    const auto version_mismatch = daq_capability_test::validate_xnavi_function_table(&table);
    assert(!version_mismatch.success && version_mismatch.code == "HEADER_RUNTIME_INCOMPATIBLE");
    assert(version_mismatch.message.find("expected=4.0") != std::string::npos);
    table.version = daq_capability_test::EXPECTED_DAQNAVI_TABLE_VERSION;
    table.revision++;
    assert(!daq_capability_test::validate_xnavi_function_table(&table).success);
    table.revision = daq_capability_test::EXPECTED_DAQNAVI_TABLE_REVISION;
    table.ai = 0;
    assert(!daq_capability_test::validate_xnavi_function_table(&table).success);
    const std::vector<double> input{10.0, 20.0, 11.0, 21.0, 12.0, 22.0};
    const std::vector<std::vector<double> > output =
        daq_capability_test::deinterleave_xnavi_samples(input, 2);
    assert((output[0] == std::vector<double>{10.0, 11.0, 12.0}));
    assert((output[1] == std::vector<double>{20.0, 21.0, 22.0}));

    const daq_capability_test::CapabilityInfo capability =
        daq_capability_test::xnavi_capability_from_scan_limit(16000000);
    assert(capability.max_scan_count == 16000000);
    assert(capability.max_points_per_channel == 16000000);

    daq_capability_test::OperationInfo requested;
    requested.channel_start = 0; requested.channel_count = 2;
    requested.points_per_channel = 64; requested.sample_rate_hz = 1000.0;
    requested.actual_ranges = {"V_Neg10To10", "V_Neg10To10"};
    daq_capability_test::OperationInfo actual = requested;
    actual.actual_channel_start = 0; actual.actual_channel_count = 2; actual.actual_samples = 64;
    actual.sample_rate_hz = 999.9995;
    assert(daq_capability_test::validate_xnavi_config_readback(requested, actual).success);
    actual.actual_samples = 63;
    const auto mismatch = daq_capability_test::validate_xnavi_config_readback(requested, actual);
    assert(!mismatch.success);
    assert(mismatch.code == "CONFIG_READBACK_MISMATCH");
    assert(mismatch.stage == "configure_readback");
    assert(mismatch.message.find("samples") != std::string::npos);

    const auto incompatible = daq_capability_test::classify_xnavi_missing_export("AdxDaqNaviLibInitialize");
    assert(incompatible.code == "HEADER_RUNTIME_INCOMPATIBLE");
    assert(incompatible.stage == "runtime_exports");
    assert(incompatible.message.find("AdxDaqNaviLibInitialize") != std::string::npos);
    const auto missing_helper = daq_capability_test::classify_xnavi_missing_export("AdxEnumToString");
    assert(missing_helper.code == "ENTRY_POINT_MISSING");
    assert(missing_helper.stage == "runtime_exports");
    assert(missing_helper.message.find("AdxEnumToString") != std::string::npos);

    std::vector<std::vector<double> > chunked(2, std::vector<double>(3));
    daq_capability_test::write_xnavi_interleaved_chunk({10.0, 20.0, 11.0}, 0, chunked);
    daq_capability_test::write_xnavi_interleaved_chunk({21.0, 12.0, 22.0}, 3, chunked);
    assert((chunked[0] == std::vector<double>{10.0, 11.0, 12.0}));
    assert((chunked[1] == std::vector<double>{20.0, 21.0, 22.0}));

    const std::vector<std::string> required = daq_capability_test::xnavi_required_runtime_exports();
    assert(required.size() == 1);
    assert(required[0] == "AdxDaqNaviLibInitialize");

    const std::vector<double> layout_values{2.0,0.1,3.0,0.2,4.0,0.3,5.0,0.4,
                                             6.0,0.5,7.0,0.6,8.0,0.7,9.0,0.8};
    assert(daq_capability_test::xnavi_demo_layout_matches_ranges(layout_values, 1.0e-6));
    std::vector<double> bad_layout=layout_values; bad_layout[3]=1.5;
    assert(!daq_capability_test::xnavi_demo_layout_matches_ranges(bad_layout, 1.0e-6));
}
