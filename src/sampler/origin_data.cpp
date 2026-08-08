#include "sampler.hpp"

#include "../constant.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
const char* const instant_v2_marker = "#HALF_SAMPLE_INSTANT_AI_V2";
const char* const instant_v1_marker = "#HALF_SAMPLE_INSTANT_AI_V1";
const char* const v2_columns = "planned_seconds,actual_seconds,voltage,read_success,read_error_code";
const char* const v1_columns = "waveform_index,planned_seconds,actual_seconds,voltage";

std::string trim_cr(std::string value) {
    if (!value.empty() && value[value.size() - 1] == '\r') value.resize(value.size() - 1);
    return value;
}

std::string trim_space(const std::string& value) {
    const std::string whitespace = " \t\r\n";
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) return std::string();
    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

bool split_exact(const std::string& row, std::size_t count, std::vector<std::string>& fields) {
    fields.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = row.find(',', start);
        fields.push_back(trim_space(row.substr(start, comma == std::string::npos ? comma : comma - start)));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return fields.size() == count;
}

bool parse_double(const std::string& text, double& value, bool allow_nan = false) {
    try {
        std::size_t consumed = 0;
        value = std::stod(text, &consumed);
        if (consumed != text.size() || std::isinf(value)) return false;
        return allow_nan || std::isfinite(value);
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_int(const std::string& text, int& value) {
    try {
        std::size_t consumed = 0;
        const long long parsed = std::stoll(text, &consumed);
        if (consumed != text.size() || parsed < std::numeric_limits<int>::min() ||
            parsed > std::numeric_limits<int>::max()) return false;
        value = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_key_double(const std::string& line, const char* key, double& value) {
    const std::string prefix = std::string(key) + "=";
    return line.compare(0, prefix.size(), prefix) == 0 && parse_double(line.substr(prefix.size()), value);
}

bool parse_key_int(const std::string& line, const char* key, int& value) {
    const std::string prefix = std::string(key) + "=";
    return line.compare(0, prefix.size(), prefix) == 0 && parse_int(line.substr(prefix.size()), value);
}

bool read_line(std::istream& input, std::string& line) {
    if (!std::getline(input, line)) return false;
    line = trim_cr(line);
    return true;
}

bool close_enough(double left, double right) {
    return std::fabs(left - right) <= 1e-10 * std::max(1.0, std::max(std::fabs(left), std::fabs(right)));
}

void set_load_error(Result::SamplingResult& result, Error::Code code = Error::INVALID_INSTANT_AI_CONFIG) {
    result.error_code = code;
}

void reset_loaded_result(Result::SamplingResult& result, const Config::SamplingConfig& config) {
    result.instant_ai_waveforms.clear();
    result.instant_ai_readings.clear();
    result.instant_ai_format_version = 0;
    result.instant_ai_complete_waveforms = 0;
    result.instant_ai_actual_duration_seconds = 0.0;
    result.instant_ai_late_reads = 0;
    result.instant_ai_interpolated_bins = 0;
    result.cancelled = false;
    result.progress.reset(config.is_instant() ? config.instant_ai_planned_duration_seconds : 0.0,
                          config.is_instant() ? config.number_of_waveforms + 1 : 0);
    result.success = false;
    result.error_code = Error::SUCCESS;
    result.maximum = 0.0;
    result.minimum = 0.0;
    result.estimate = Estimate::EstimatedResult();
}

void write_number(std::ostream& output, double value) {
    if (std::isnan(value)) output << "nan";
    else output << std::setprecision(15) << value;
}

void write_metadata_number(std::ostream& output, double value) {
    if (std::isnan(value)) {
        output << "nan";
        return;
    }
    std::string best;
    for (int precision = 1; precision <= std::numeric_limits<double>::max_digits10; ++precision) {
        std::ostringstream candidate;
        candidate << std::setprecision(precision) << value;
        double reparsed = 0.0;
        const std::string text = candidate.str();
        if (parse_double(text, reparsed) && reparsed == value && (best.empty() || text.size() < best.size()))
            best = text;
    }
    if (!best.empty()) output << best;
    else output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
}

void write_reading_number(std::ostream& output, double value) {
    if (std::isnan(value)) output << "nan";
    else output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
}

bool fail_dump(Result::SamplingResult& result) {
    if (result.error_code == Error::SUCCESS) result.error_code = Error::FILE_NOT_FOUND;
    return false;
}

bool load_v2(std::istream& input, Config::SamplingConfig& config, Result::SamplingResult& result) {
    std::string frequency_line, points_line, waveforms_line, polling_line, planned_line, actual_line, header;
    if (!read_line(input, frequency_line) || !read_line(input, points_line) || !read_line(input, waveforms_line) ||
        !read_line(input, polling_line) || !read_line(input, planned_line) || !read_line(input, actual_line) ||
        !read_line(input, header)) return false;

    double frequency = 0.0, max_polling = 0.0, planned_duration = 0.0, actual_duration = 0.0;
    int points = 0, waveforms = 0;
    if (!parse_key_double(frequency_line, "emitting_frequency", frequency) ||
        !parse_key_int(points_line, "target_points", points) ||
        !parse_key_int(waveforms_line, "number_of_waveforms", waveforms) ||
        !parse_key_double(polling_line, "max_reliable_polling_hz", max_polling) ||
        !parse_key_double(planned_line, "planned_duration_seconds", planned_duration) ||
        !parse_key_double(actual_line, "actual_duration_seconds", actual_duration) || actual_duration < 0.0 ||
        header != v2_columns) return false;

    Config::SamplingConfig loaded_config = config;
    if (!loaded_config.update(waveforms, frequency, frequency, points, max_polling) ||
        !close_enough(planned_duration, loaded_config.instant_ai_planned_duration_seconds)) return false;

    Sampler::InstantAi::TimedReadings readings;
    std::string row;
    std::vector<std::string> fields;
    const std::size_t reconstruction_rows = static_cast<std::size_t>(waveforms) + 1u;
    while (read_line(input, row)) {
        if (row.empty()) continue;
        if (readings.size() >= static_cast<std::size_t>(Constant::MaxSamplingPoints)) return false;
        const std::size_t new_size = readings.size() + 1u;
        if (new_size > Constant::MaxInstantAiReconstructionCells / reconstruction_rows) return false;
        double planned = 0.0, actual = 0.0, voltage = 0.0;
        int succeeded = 0, error_code = 0;
        if (!split_exact(row, 5, fields) || !parse_double(fields[0], planned) ||
            !parse_double(fields[1], actual) || !parse_double(fields[2], voltage, true) ||
            !parse_int(fields[3], succeeded) || (succeeded != 0 && succeeded != 1) ||
            !parse_int(fields[4], error_code) || (succeeded && !std::isfinite(voltage))) return false;
        readings.push_back({planned, actual, voltage, succeeded != 0, error_code});
    }
    if (input.bad()) return false;

    config = loaded_config;
    reset_loaded_result(result, config);
    result.instant_ai_format_version = 2;
    result.instant_ai_readings.swap(readings);
    result.instant_ai_actual_duration_seconds = actual_duration;
    result.totalSamplingBuffer.clear();
    result.resultWave.assign(static_cast<std::size_t>(config.valid_length), 0.0);
    return true;
}

bool load_v1(std::istream& input, Config::SamplingConfig& config, Result::SamplingResult& result) {
    std::string frequency_line, points_line, waveforms_line, header;
    if (!read_line(input, frequency_line) || !read_line(input, points_line) || !read_line(input, waveforms_line) ||
        !read_line(input, header)) return false;
    double frequency = 0.0;
    int points = 0, waveforms = 0;
    if (!parse_key_double(frequency_line, "emitting_frequency", frequency) ||
        !parse_key_int(points_line, "target_points", points) ||
        !parse_key_int(waveforms_line, "number_of_waveforms", waveforms) || header != v1_columns) return false;

    Config::SamplingConfig loaded_config = config;
    if (!loaded_config.update(waveforms, frequency, frequency, points)) return false;
    std::vector<Sampler::InstantAi::TimedWaveform> loaded_waveforms(static_cast<std::size_t>(waveforms));
    std::vector<double> voltages;
    std::string row;
    std::vector<std::string> fields;
    while (read_line(input, row)) {
        if (row.empty()) continue;
        if (voltages.size() >= static_cast<std::size_t>(Constant::MaxSamplingPoints)) return false;
        int waveform = 0;
        double planned = 0.0, actual = 0.0, voltage = 0.0;
        if (!split_exact(row, 4, fields) || !parse_int(fields[0], waveform) || waveform < 0 ||
            waveform >= waveforms || !parse_double(fields[1], planned) || !parse_double(fields[2], actual) ||
            !parse_double(fields[3], voltage)) return false;
        loaded_waveforms[static_cast<std::size_t>(waveform)].push_back({planned, actual, voltage});
        voltages.push_back(voltage);
    }
    if (input.bad()) return false;

    config = loaded_config;
    reset_loaded_result(result, config);
    result.instant_ai_format_version = 1;
    result.instant_ai_waveforms.swap(loaded_waveforms);
    result.totalSamplingBuffer.swap(voltages);
    result.resultWave.assign(static_cast<std::size_t>(config.valid_length), 0.0);
    return true;
}

bool load_buffered(std::istream& input, Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.sampling_length_per_sample <= 0 || config.sampling_time <= 0) return false;
    const std::size_t per_sample = static_cast<std::size_t>(config.sampling_length_per_sample);
    const std::size_t times = static_cast<std::size_t>(config.sampling_time);
    if (times > static_cast<std::size_t>(Constant::MaxSamplingPoints) / per_sample) return false;
    const std::size_t capacity = per_sample * times;
    std::vector<double> values;
    values.reserve(std::min(capacity, static_cast<std::size_t>(4096)));
    std::string token;
    while (std::getline(input, token, ',')) {
        if (values.size() >= capacity || values.size() >= static_cast<std::size_t>(Constant::MaxSamplingPoints))
            return false;
        double value = 0.0;
        if (!parse_double(trim_space(token), value)) return false;
        values.push_back(value);
    }
    // The historical Buffered writer stores exactly one configured acquisition block,
    // even when the in-memory buffer has room for several RunOnce calls.
    if (input.bad() || values.size() != per_sample) return false;
    values.resize(capacity, 0.0);

    config.acquisition_mode = Config::AcquisitionMode::Buffered;
    reset_loaded_result(result, config);
    result.totalSamplingBuffer.swap(values);
    result.resultWave.assign(static_cast<std::size_t>(std::max(0, config.valid_length)), 0.0);
    return true;
}
} // namespace

namespace Sampler {
bool Sampler::dump_origin_data(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.dump_file_path.empty()) return true;
    std::ofstream output(config.dump_file_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) return fail_dump(result);
    if (config.is_instant()) {
        output << instant_v2_marker << '\n' << "emitting_frequency=";
        write_metadata_number(output, config.emitting_frequency);
        output << '\n' << "target_points=" << config.instant_ai_target_points_per_waveform << '\n'
               << "number_of_waveforms=" << config.number_of_waveforms << '\n'
               << "max_reliable_polling_hz=";
        write_metadata_number(output, config.instant_ai_max_reliable_polling_hz);
        output << '\n' << "planned_duration_seconds=";
        write_metadata_number(output, config.instant_ai_planned_duration_seconds);
        output << '\n' << "actual_duration_seconds=";
        write_metadata_number(output, result.instant_ai_actual_duration_seconds);
        output << '\n' << v2_columns << '\n';
        for (const auto& reading : result.instant_ai_readings) {
            write_reading_number(output, reading.planned_seconds);
            output << ',';
            write_reading_number(output, reading.actual_seconds);
            output << ',';
            write_reading_number(output, reading.voltage);
            output << ',' << (reading.read_success ? 1 : 0) << ',' << reading.read_error_code << '\n';
        }
    } else {
        if (config.sampling_length_per_sample < 0 ||
            result.totalSamplingBuffer.size() < static_cast<std::size_t>(config.sampling_length_per_sample))
            return fail_dump(result);
        for (int i = 0; i < config.sampling_length_per_sample; ++i) {
            write_number(output, result.totalSamplingBuffer[static_cast<std::size_t>(i)]);
            if (i + 1 < config.sampling_length_per_sample) output << ',';
        }
    }
    output.close();
    return output.good() ? true : fail_dump(result);
}

bool Sampler::load_origin_data(Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.dump_file_path.empty()) {
        set_load_error(result, Error::FILE_NOT_FOUND);
        return false;
    }
    std::ifstream input(config.dump_file_path, std::ios::binary);
    if (!input.is_open()) {
        set_load_error(result, Error::FILE_NOT_FOUND);
        return false;
    }
    std::string first_line;
    if (!read_line(input, first_line)) {
        set_load_error(result);
        return false;
    }
    bool loaded = false;
    if (first_line == instant_v2_marker) loaded = load_v2(input, config, result);
    else if (first_line == instant_v1_marker) loaded = load_v1(input, config, result);
    else if (first_line.compare(0, 12, "#HALF_SAMPLE") != 0) {
        input.clear();
        input.seekg(0);
        loaded = load_buffered(input, config, result);
    }
    if (!loaded) set_load_error(result);
    return loaded;
}
} // namespace Sampler
