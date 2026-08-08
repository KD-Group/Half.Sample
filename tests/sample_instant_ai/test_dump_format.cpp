#include "../../src/sampler/mock_sampler.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
const char* const v2_path = "cpp_build/instant-ai-v2-test.csv";
const char* const fixture_path = "cpp_build/instant-ai-fixture-test.csv";

void write_text(const char* path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    assert(output.is_open());
    output << text;
    assert(output.good());
}

std::string read_text(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

std::string valid_v2_header(int waveforms = 1) {
    std::ostringstream text;
    text << "#HALF_SAMPLE_INSTANT_AI_V2\n"
         << "emitting_frequency=0.05\n"
         << "target_points=100\n"
         << "number_of_waveforms=" << waveforms << "\n"
         << "max_reliable_polling_hz=10\n"
         << "planned_duration_seconds=" << (waveforms + 1) / 0.05 << "\n"
         << "actual_duration_seconds=40\n"
         << "planned_seconds,actual_seconds,voltage,read_success,read_error_code\n";
    return text.str();
}

class DecimalComma : public std::numpunct<char> {
  protected:
    char do_decimal_point() const override { return ','; }
};

void assert_failed_load_preserves_data(const std::string& contents) {
    write_text(fixture_path, contents);
    Config::SamplingConfig config;
    assert(config.update(2, 20.0));
    config.dump_file_path = fixture_path;
    const double old_frequency = config.emitting_frequency;
    Result::SamplingResult result;
    result.totalSamplingBuffer.assign(1, 123.0);
    result.instant_ai_readings.push_back({7.0, 8.0, 9.0});
    result.instant_ai_format_version = 77;

    bool loaded = true;
    try {
        loaded = Sampler::Sampler::load_origin_data(config, result);
    } catch (...) {
        assert(false && "malformed files must not throw");
    }
    assert(!loaded);
    assert(result.error_code == Error::INVALID_INSTANT_AI_CONFIG);
    assert(config.emitting_frequency == old_frequency);
    assert(result.totalSamplingBuffer.size() == 1 && result.totalSamplingBuffer[0] == 123.0);
    assert(result.instant_ai_readings.size() == 1 && result.instant_ai_readings[0].voltage == 9.0);
    assert(result.instant_ai_format_version == 77);
}
} // namespace

void test_dump_format() {
    std::remove(v2_path);
    std::remove(fixture_path);

    {
        Config::SamplingConfig config;
        assert(config.update(1, 0.05, 0.1, 100, 10.0));
        config.dump_file_path = v2_path;
        Result::SamplingResult result;
        result.instant_ai_format_version = 2;
        result.instant_ai_actual_duration_seconds = 40.0;
        result.instant_ai_readings.push_back({0.0, 0.01, 5.0, true, 0, 0.02});
        result.instant_ai_readings.push_back({0.2, 0.35, std::numeric_limits<double>::quiet_NaN(), false,
                                              static_cast<int>(Error::ErrorDeviceIoTimeOut), 0.40});
        result.instant_ai_readings.push_back({std::nextafter(1.0, 2.0), std::nextafter(2.0, 3.0),
                                              std::nextafter(3.0, 4.0), true, 0});
        assert(Sampler::Sampler::dump_origin_data(config, result));

        const std::string expected = valid_v2_header() +
            "0,0.01,5,1,0\n"
            "0.20000000000000001,0.34999999999999998,nan,0,-536870884\n"
            "1.0000000000000002,2.0000000000000004,3.0000000000000004,1,0\n";
        assert(read_text(v2_path) == expected);

        config.update(2, 20.0);
        config.dump_file_path = v2_path;
        result.totalSamplingBuffer.assign(1, 999.0);
        result.instant_ai_waveforms.assign(1, Sampler::InstantAi::TimedWaveform(1));
        result.instant_ai_readings.assign(1, Sampler::InstantAi::TimedReading(9.0, 9.0, 9.0));
        result.instant_ai_actual_duration_seconds = -1.0;
        result.instant_ai_format_version = 0;
        result.success = true;
        assert(Sampler::Sampler::load_origin_data(config, result));
        assert(config.is_instant());
        assert(config.emitting_frequency == 0.05);
        assert(config.instant_ai_target_points_per_waveform == 100);
        assert(config.number_of_waveforms == 1);
        assert(config.instant_ai_max_reliable_polling_hz == 10.0);
        assert(config.instant_ai_planned_duration_seconds == 40.0);
        assert(result.instant_ai_actual_duration_seconds == 40.0);
        assert(result.instant_ai_format_version == 2);
        assert(!result.success);
        assert(result.instant_ai_waveforms.empty());
        assert(result.totalSamplingBuffer.empty());
        assert(result.instant_ai_readings.size() == 3);
        assert(result.instant_ai_readings[0].read_success);
        assert(result.instant_ai_readings[0].actual_seconds == 0.01);
        assert(!result.instant_ai_readings[1].read_success);
        assert(std::isnan(result.instant_ai_readings[1].voltage));
        assert(result.instant_ai_readings[1].read_error_code == static_cast<int>(Error::ErrorDeviceIoTimeOut));
        assert(result.instant_ai_readings[2].planned_seconds == std::nextafter(1.0, 2.0));
        assert(result.instant_ai_readings[2].actual_seconds == std::nextafter(2.0, 3.0));
        assert(result.instant_ai_readings[2].voltage == std::nextafter(3.0, 4.0));
    }

    {
        const double frequency = std::nextafter(0.05, 0.1);
        const double maximum_polling = std::nextafter(10.0, 11.0);
        const double actual_duration = std::nextafter(40.0, 41.0);
        Config::SamplingConfig config;
        assert(config.update(1, frequency, 0.1, 100, maximum_polling));
        config.dump_file_path = v2_path;
        Result::SamplingResult result;
        result.instant_ai_format_version = 2;
        result.instant_ai_actual_duration_seconds = actual_duration;
        assert(Sampler::Sampler::dump_origin_data(config, result));

        assert(config.update(2, 20.0));
        config.dump_file_path = v2_path;
        result.instant_ai_actual_duration_seconds = 0.0;
        assert(Sampler::Sampler::load_origin_data(config, result));
        assert(config.emitting_frequency == frequency);
        assert(config.instant_ai_max_reliable_polling_hz == maximum_polling);
        assert(result.instant_ai_actual_duration_seconds == actual_duration);
    }

    {
        write_text(fixture_path,
                   "#HALF_SAMPLE_INSTANT_AI_V1\n"
                   "emitting_frequency=0.05\n"
                   "target_points=100\n"
                   "number_of_waveforms=1\n"
                   "waveform_index,planned_seconds,actual_seconds,voltage\n"
                   "0,0,0.01,5\n");
        Config::SamplingConfig config;
        assert(config.update(2, 20.0));
        config.dump_file_path = fixture_path;
        Result::SamplingResult result;
        result.instant_ai_readings.push_back({1.0, 1.0, 1.0});
        assert(Sampler::Sampler::load_origin_data(config, result));
        assert(result.instant_ai_format_version == 1);
        assert(result.instant_ai_readings.empty());
        assert(result.instant_ai_waveforms.size() == 1);
        assert(result.instant_ai_waveforms[0].size() == 1);
    }

    {
        write_text(fixture_path, "1.5,-2,3.25");
        Config::SamplingConfig config;
        assert(config.update(1, 100.0));
        config.sampling_length_per_sample = 3;
        config.sampling_time = 1;
        config.dump_file_path = fixture_path;
        Result::SamplingResult result;
        result.totalSamplingBuffer.clear();
        result.instant_ai_readings.push_back({1.0, 1.0, 1.0});
        assert(Sampler::Sampler::load_origin_data(config, result));
        assert(result.instant_ai_format_version == 0);
        assert(result.instant_ai_readings.empty());
        assert(result.totalSamplingBuffer.size() == 3);
        assert(result.totalSamplingBuffer[0] == 1.5);
        assert(result.totalSamplingBuffer[1] == -2.0);
        assert(result.totalSamplingBuffer[2] == 3.25);
    }

    {
        write_text(fixture_path, "1,2,3");
        Config::SamplingConfig config;
        assert(config.update(1, 100.0));
        config.sampling_length_per_sample = 3;
        config.sampling_time = 3;
        config.dump_file_path = fixture_path;
        Result::SamplingResult result(false);
        assert(Sampler::Sampler::load_origin_data(config, result));
        assert(result.totalSamplingBuffer.size() == 9);
        assert(result.totalSamplingBuffer[0] == 1.0);
        assert(result.totalSamplingBuffer[2] == 3.0);
        assert(result.totalSamplingBuffer[3] == 0.0);
        assert(result.totalSamplingBuffer[8] == 0.0);
    }

    {
        write_text(fixture_path, "1,2,3,4,5,6,7,8");
        Config::SamplingConfig config;
        assert(config.update(1, 100.0));
        config.sampling_length_per_sample = 8;
        config.sampling_time = 6000000;
        config.dump_file_path = fixture_path;
        Result::SamplingResult result(false);
        assert(Sampler::Sampler::load_origin_data(config, result));
        assert(result.totalSamplingBuffer.size() == static_cast<std::size_t>(Constant::MaxBufferSize));
        assert(result.totalSamplingBuffer[7] == 8.0);
        assert(result.totalSamplingBuffer.back() == 0.0);
    }

    {
        const std::locale previous = std::locale::global(std::locale(std::locale::classic(), new DecimalComma));
        Config::SamplingConfig config;
        assert(config.update(1, 0.05, 0.1, 100, 10.0));
        config.dump_file_path = v2_path;
        Result::SamplingResult result(false);
        result.instant_ai_actual_duration_seconds = 40.0;
        result.instant_ai_readings.push_back({0.25, 0.5, 1.25, true, 0});
        assert(Sampler::Sampler::dump_origin_data(config, result));
        assert(read_text(v2_path).find("emitting_frequency=0.05\n") != std::string::npos);
        Config::SamplingConfig replay_config;
        replay_config.dump_file_path = v2_path;
        Result::SamplingResult replay(false);
        assert(Sampler::Sampler::load_origin_data(replay_config, replay));
        assert(replay.instant_ai_readings[0].voltage == 1.25);
        std::locale::global(previous);
    }

    {
        std::string crlf = valid_v2_header();
        std::string converted;
        for (char value : crlf) converted += value == '\n' ? "\r\n" : std::string(1, value);
        converted += "0,0,5,1,0\r\n";
        write_text(fixture_path, converted);
        Config::SamplingConfig config;
        config.dump_file_path = fixture_path;
        Result::SamplingResult result(false);
        assert(Sampler::Sampler::load_origin_data(config, result));
        assert(result.instant_ai_readings.size() == 1);
    }

    assert_failed_load_preserves_data("#HALF_SAMPLE_FUTURE\nanything\n");
    assert_failed_load_preserves_data("");
    assert_failed_load_preserves_data("#HALF_SAMPLE_INSTANT_AI_V2\nemitting_frequency=oops\n");
    assert_failed_load_preserves_data(valid_v2_header() + "0,0,5,maybe,0\n");
    assert_failed_load_preserves_data(valid_v2_header() + "0,0,nan,1,0\n");
    assert_failed_load_preserves_data(valid_v2_header() + "-0.1,0,5,1,0\n");
    assert_failed_load_preserves_data(valid_v2_header() + "41,0,5,1,0\n");
    assert_failed_load_preserves_data(valid_v2_header() + "0,41,5,1,0\n");
    assert_failed_load_preserves_data(valid_v2_header() + "0,0,5,1,7\n");
    assert_failed_load_preserves_data(valid_v2_header() + "0,0,5,0,7\n");
    assert_failed_load_preserves_data(valid_v2_header() + "0,0,nan,0,0\n");
    assert_failed_load_preserves_data(valid_v2_header() + "0,0,nan,0,7,\n");
    assert_failed_load_preserves_data(
        "#HALF_SAMPLE_INSTANT_AI_V2\nemitting_frequency=" + std::string(4096, '0') +
        "0.05\ntarget_points=100\nnumber_of_waveforms=1\nmax_reliable_polling_hz=10\n"
        "planned_duration_seconds=40\nactual_duration_seconds=40\n" +
        "planned_seconds,actual_seconds,voltage,read_success,read_error_code\n");
    assert_failed_load_preserves_data(valid_v2_header() + std::string(4096, '0') + "0,0,5,1,0\n");
    assert_failed_load_preserves_data(
        "#HALF_SAMPLE_INSTANT_AI_V2\n"
        "emitting_frequency=0.05\n"
        "target_points=8000001\n"
        "number_of_waveforms=1\n"
        "max_reliable_polling_hz=10\n"
        "planned_duration_seconds=40\n"
        "actual_duration_seconds=1\n"
        "planned_seconds,actual_seconds,voltage,read_success,read_error_code\n");

    {
        Config::SamplingConfig config;
        assert(config.update(1, 100.0));
        config.sampling_length_per_sample = 3;
        config.sampling_time = 1;
        config.dump_file_path = fixture_path;
        write_text(fixture_path, "1,2");
        Result::SamplingResult result;
        result.totalSamplingBuffer.assign(1, 123.0);
        assert(!Sampler::Sampler::load_origin_data(config, result));
        assert(result.error_code == Error::INVALID_INSTANT_AI_CONFIG);
        assert(result.totalSamplingBuffer.size() == 1 && result.totalSamplingBuffer[0] == 123.0);
    }

    {
        Config::SamplingConfig config;
        assert(config.update(1, 100.0));
        config.sampling_length_per_sample = 3;
        config.sampling_time = 1;
        config.dump_file_path = fixture_path;
        write_text(fixture_path, "1,2,3,");
        Result::SamplingResult result(false);
        assert(!Sampler::Sampler::load_origin_data(config, result));
        assert(result.error_code == Error::INVALID_INSTANT_AI_CONFIG);
    }

    {
        Config::SamplingConfig config;
        assert(config.update(1, 100.0));
        config.sampling_length_per_sample = 1;
        config.sampling_time = 1;
        config.dump_file_path = fixture_path;
        write_text(fixture_path, std::string(4096, '0') + "1");
        Result::SamplingResult result(false);
        assert(!Sampler::Sampler::load_origin_data(config, result));
        assert(result.error_code == Error::INVALID_INSTANT_AI_CONFIG);
    }

    {
        Config::SamplingConfig config;
        assert(config.update(1, 100.0));
        config.sampling_length_per_sample = 3;
        config.dump_file_path = fixture_path;
        write_text(fixture_path, "original-content");
        Result::SamplingResult result(false);
        result.totalSamplingBuffer.assign(2, 1.0);
        assert(!Sampler::Sampler::dump_origin_data(config, result));
        assert(read_text(fixture_path) == "original-content");
    }

    {
        Config::SamplingConfig config;
        assert(config.update(1, 100.0));
        config.sampling_length_per_sample = 3;
        config.dump_file_path = fixture_path;
        write_text(fixture_path, "keep-finite-original");
        Result::SamplingResult result(false);
        result.totalSamplingBuffer = {1.0, std::numeric_limits<double>::quiet_NaN(), 3.0};
        assert(!Sampler::Sampler::dump_origin_data(config, result));
        assert(read_text(fixture_path) == "keep-finite-original");
    }

#ifdef _WIN32
    {
        const std::string unicode_path = "cpp_build/\xE6\xB5\x8B\xE9\x87\x8F.csv";
        Config::SamplingConfig config;
        assert(config.update(1, 0.05, 0.1, 100, 10.0));
        config.dump_file_path = unicode_path;
        Result::SamplingResult original(false);
        original.instant_ai_actual_duration_seconds = 40.0;
        original.instant_ai_readings.push_back({0.0, 0.0, 5.0, true, 0});
        assert(Sampler::Sampler::dump_origin_data(config, original));
        assert(GetFileAttributesW(L"cpp_build/\x6D4B\x91CF.csv") != INVALID_FILE_ATTRIBUTES);

        Config::SamplingConfig replay_config;
        replay_config.dump_file_path = unicode_path;
        Result::SamplingResult replay(false);
        assert(Sampler::Sampler::load_origin_data(replay_config, replay));
        assert(replay.instant_ai_readings.size() == 1);
        assert(replay.instant_ai_readings[0].voltage == 5.0);

        Config::SamplingConfig invalid_source_config;
        assert(invalid_source_config.update(1, 100.0));
        invalid_source_config.sampling_length_per_sample = 3;
        invalid_source_config.dump_file_path = unicode_path;
        Result::SamplingResult invalid_source(false);
        invalid_source.totalSamplingBuffer.assign(2, 1.0);
        assert(!Sampler::Sampler::dump_origin_data(invalid_source_config, invalid_source));
        assert(Sampler::Sampler::load_origin_data(replay_config, replay));
        assert(replay.instant_ai_readings[0].voltage == 5.0);

        const std::string invalid_utf8 = std::string("cpp_build/invalid-") + static_cast<char>(0xff) + ".csv";
        config.dump_file_path = invalid_utf8;
        original.error_code = Error::SUCCESS;
        assert(!Sampler::Sampler::dump_origin_data(config, original));
        assert(original.error_code == Error::FILE_NOT_FOUND);
        Config::SamplingConfig invalid_load_config;
        invalid_load_config.dump_file_path = invalid_utf8;
        Result::SamplingResult invalid_load(false);
        assert(!Sampler::Sampler::load_origin_data(invalid_load_config, invalid_load));
        assert(invalid_load.error_code == Error::FILE_NOT_FOUND);
        assert(Sampler::Sampler::load_origin_data(replay_config, replay));
        _wremove(L"cpp_build/\x6D4B\x91CF.csv");
    }
#endif

    {
        std::ostringstream oversized;
        oversized << valid_v2_header(198);
        for (int row = 0; row < 20101; ++row)
            oversized << row / 10.0 << ',' << row / 10.0 << ",0,1,0\n";
        assert_failed_load_preserves_data(oversized.str());
    }

    {
        Config::SamplingConfig config;
        assert(config.update(1, 0.05, 0.1, 100, 10.0));
        config.dump_file_path = "cpp_build/no-such-directory/instant.csv";
        Result::SamplingResult result;
        result.progress.request_cancel();
        Sampler::MockSampler mock;
        assert(!mock.sample(config, result));
        assert(result.error_code == Error::USER_CANCELLED);
        assert(result.cancelled);
    }

    {
        Config::SamplingConfig config;
        assert(config.update(1, 0.05, 0.1, 100, 10.0));
        config.dump_file_path = fixture_path;
        Result::SamplingResult result;
        result.progress.request_cancel();
        Sampler::MockSampler mock;
        assert(!mock.sample(config, result));
        assert(read_text(fixture_path).find("#HALF_SAMPLE_INSTANT_AI_V2\n") == 0);
    }

    {
        Config::SamplingConfig config;
        assert(config.update(1, 0.05, 0.1, 100, 10.0));
        config.dump_file_path = "cpp_build/no-such-directory/instant.csv";
        Result::SamplingResult result;
        Sampler::MockSampler mock;
        assert(!mock.sample(config, result));
        assert(result.error_code == Error::FILE_NOT_FOUND);
    }

    std::remove(v2_path);
    std::remove(fixture_path);
}
