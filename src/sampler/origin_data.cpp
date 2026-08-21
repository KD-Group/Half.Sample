#include "sampler.hpp"

#include "../constant.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace {
const char* const instant_v2_marker = "#HALF_SAMPLE_INSTANT_AI_V2";
const char* const instant_v1_marker = "#HALF_SAMPLE_INSTANT_AI_V1";
const char* const v2_columns = "planned_seconds,actual_seconds,voltage,read_success,read_error_code";
const char* const v1_columns = "waveform_index,planned_seconds,actual_seconds,voltage";
const std::size_t max_marker_length = 64u;
const std::size_t max_metadata_length = 256u;
const std::size_t max_row_length = 512u;
const std::size_t max_buffered_token_length = 128u;

enum class ReadStatus { Record, End, TooLong, IoError };

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
    if (allow_nan && text == "nan") {
        value = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    std::istringstream parser(text);
    parser.imbue(std::locale::classic());
    parser >> std::noskipws >> value;
    return parser && parser.eof() && std::isfinite(value);
}

bool parse_int(const std::string& text, int& value) {
    long long parsed = 0;
    std::istringstream parser(text);
    parser.imbue(std::locale::classic());
    parser >> std::noskipws >> parsed;
    if (!parser || !parser.eof() || parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) return false;
    value = static_cast<int>(parsed);
    return true;
}

bool parse_key_double(const std::string& line, const char* key, double& value) {
    const std::string prefix = std::string(key) + "=";
    return line.compare(0, prefix.size(), prefix) == 0 && parse_double(line.substr(prefix.size()), value);
}

bool parse_key_int(const std::string& line, const char* key, int& value) {
    const std::string prefix = std::string(key) + "=";
    return line.compare(0, prefix.size(), prefix) == 0 && parse_int(line.substr(prefix.size()), value);
}

ReadStatus read_bounded_line(std::istream& input, std::string& line, std::size_t maximum) {
    line.clear();
    char value = 0;
    while (input.get(value)) {
        if (value == '\n') {
            line = trim_cr(line);
            return ReadStatus::Record;
        }
        if (line.size() >= maximum) return ReadStatus::TooLong;
        line.push_back(value);
    }
    if (input.bad()) return ReadStatus::IoError;
    if (line.empty()) return ReadStatus::End;
    line = trim_cr(line);
    return ReadStatus::Record;
}

ReadStatus read_bounded_field(std::istream& input, std::string& field, char delimiter,
                              std::size_t maximum, bool& ended_by_delimiter) {
    field.clear();
    ended_by_delimiter = false;
    char value = 0;
    while (input.get(value)) {
        if (value == delimiter) {
            ended_by_delimiter = true;
            return ReadStatus::Record;
        }
        if (field.size() >= maximum) return ReadStatus::TooLong;
        field.push_back(value);
    }
    if (input.bad()) return ReadStatus::IoError;
    return field.empty() ? ReadStatus::End : ReadStatus::Record;
}

bool read_required_line(std::istream& input, std::string& line, std::size_t maximum = max_metadata_length) {
    return read_bounded_line(input, line, maximum) == ReadStatus::Record;
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
    result.independent_cycle_vmax_accumulator.clear();
    result.independent_cycle_vmin_accumulator.clear();
    result.independent_cycle_vpp_accumulator.clear();
    result.independent_cycle_voltage_amplitude_accumulator.clear();
    result.cancelled = false;
    result.progress.reset(config.is_instant() ? config.instant_ai_planned_duration_seconds : 0.0,
                          config.is_instant() ? config.number_of_waveforms + 1 : 0);
    result.success = false;
    result.error_code = Error::SUCCESS;
    result.maximum = 0.0;
    result.minimum = 0.0;
    result.cycle_vmax = 0.0;
    result.cycle_vmin = 0.0;
    result.cycle_vpp = 0.0;
    result.cycle_vtop = 0.0;
    result.cycle_vbase = 0.0;
    result.cycle_maximum = 0.0;
    result.cycle_minimum = 0.0;
    result.v_inf = 0.0;
    result.v_inf_valid = false;
    result.estimate = Estimate::EstimatedResult();
}

void write_number(std::ostream& output, double value) {
    if (std::isnan(value)) output << "nan";
    else output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
}

void write_metadata_number(std::ostream& output, double value) {
    if (std::isnan(value)) {
        output << "nan";
        return;
    }
    std::string best;
    for (int precision = 1; precision <= std::numeric_limits<double>::max_digits10; ++precision) {
        std::ostringstream candidate;
        candidate.imbue(std::locale::classic());
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

bool fail_dump(Result::SamplingResult& result, Error::Code code = Error::FILE_NOT_FOUND) {
    if (result.error_code == Error::SUCCESS) result.error_code = code;
    return false;
}

std::size_t buffered_capacity(const Config::SamplingConfig& config) {
    if (config.sampling_length_per_sample <= 0 || config.sampling_time <= 0) return 0;
    const std::size_t per_sample = static_cast<std::size_t>(config.sampling_length_per_sample);
    const std::size_t times = static_cast<std::size_t>(config.sampling_time);
    const std::size_t limit = static_cast<std::size_t>(Constant::MaxBufferSize);
    return times <= limit / per_sample ? per_sample * times : limit;
}

std::size_t buffered_dump_count(const Config::SamplingConfig& config) {
    if (config.waveform_processing_mode == "independent_cycle") {
        return buffered_capacity(config);
    }
    return config.sampling_length_per_sample > 0
        ? static_cast<std::size_t>(config.sampling_length_per_sample)
        : 0;
}

bool valid_dump_source(const Config::SamplingConfig& config, const Result::SamplingResult& result) {
    if (!config.is_instant()) {
        const std::size_t count = buffered_dump_count(config);
        if (count == 0 || result.totalSamplingBuffer.size() < count) return false;
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(result.totalSamplingBuffer[i])) return false;
        }
        return true;
    }
    if (!std::isfinite(config.emitting_frequency) || config.emitting_frequency <= 0.0 ||
        config.instant_ai_target_points_per_waveform < 20 || config.number_of_waveforms <= 0 ||
        !std::isfinite(config.instant_ai_max_reliable_polling_hz) ||
        config.instant_ai_max_reliable_polling_hz <= 0.0 ||
        !std::isfinite(config.instant_ai_planned_duration_seconds) ||
        config.instant_ai_planned_duration_seconds <= 0.0 ||
        !std::isfinite(result.instant_ai_actual_duration_seconds) ||
        result.instant_ai_actual_duration_seconds < 0.0) return false;
    for (const auto& reading : result.instant_ai_readings) {
        if (!std::isfinite(reading.planned_seconds) || reading.planned_seconds < 0.0 ||
            !std::isfinite(reading.actual_seconds) || reading.actual_seconds < 0.0 ||
            !std::isfinite(reading.completed_seconds) || reading.completed_seconds < reading.actual_seconds ||
            reading.planned_seconds > config.instant_ai_planned_duration_seconds ||
            reading.completed_seconds > result.instant_ai_actual_duration_seconds) return false;
        if (reading.read_success) {
            if (!std::isfinite(reading.voltage) || reading.read_error_code != 0) return false;
        } else if (!std::isnan(reading.voltage) || reading.read_error_code == 0) {
            return false;
        }
    }
    return true;
}

unsigned long process_id() {
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

#ifdef _WIN32
bool widen_path(const std::string& path, std::wstring& wide) {
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (length <= 0) return false;
    wide.assign(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, &wide[0], length) <= 0) return false;
    wide.resize(static_cast<std::size_t>(length - 1));
    return true;
}

bool path_supported(const std::string& path) {
    std::wstring wide;
    return widen_path(path, wide);
}

bool open_input(const std::string& path, std::ifstream& input) {
    std::wstring wide;
    if (!widen_path(path, wide)) return false;
    input.open(wide.c_str(), std::ios::binary);
    return input.is_open();
}

bool open_output(const std::string& path, std::ofstream& output) {
    std::wstring wide;
    if (!widen_path(path, wide)) return false;
    output.open(wide.c_str(), std::ios::binary | std::ios::trunc);
    return output.is_open();
}

bool path_exists(const std::string& path) {
    std::wstring wide;
    return widen_path(path, wide) && GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void remove_path(const std::string& path) {
    std::wstring wide;
    if (widen_path(path, wide)) _wremove(wide.c_str());
}
#else
bool path_supported(const std::string&) { return true; }

bool open_input(const std::string& path, std::ifstream& input) {
    input.open(path.c_str(), std::ios::binary);
    return input.is_open();
}

bool open_output(const std::string& path, std::ofstream& output) {
    output.open(path.c_str(), std::ios::binary | std::ios::trunc);
    return output.is_open();
}

bool path_exists(const std::string& path) {
    std::ifstream input;
    input.imbue(std::locale::classic());
    return open_input(path, input);
}

void remove_path(const std::string& path) { std::remove(path.c_str()); }
#endif

std::string unique_temporary_path(const std::string& destination) {
    static std::atomic<unsigned long long> sequence{0};
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::ostringstream name;
        name.imbue(std::locale::classic());
        name << destination << ".half-sample-tmp." << process_id() << '.' << sequence.fetch_add(1);
        if (!path_exists(name.str())) return name.str();
    }
    return std::string();
}

bool replace_atomically(const std::string& temporary, const std::string& destination) {
#ifdef _WIN32
    std::wstring temporary_wide, destination_wide;
    return widen_path(temporary, temporary_wide) && widen_path(destination, destination_wide) &&
           MoveFileExW(temporary_wide.c_str(), destination_wide.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temporary.c_str(), destination.c_str()) == 0;
#endif
}

bool load_v2(std::istream& input, Config::SamplingConfig& config, Result::SamplingResult& result) {
    std::string frequency_line, points_line, waveforms_line, polling_line, planned_line, actual_line, header;
    if (!read_required_line(input, frequency_line) || !read_required_line(input, points_line) ||
        !read_required_line(input, waveforms_line) || !read_required_line(input, polling_line) ||
        !read_required_line(input, planned_line) || !read_required_line(input, actual_line) ||
        !read_required_line(input, header)) return false;

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
    while (true) {
        const ReadStatus row_status = read_bounded_line(input, row, max_row_length);
        if (row_status == ReadStatus::End) break;
        if (row_status != ReadStatus::Record) return false;
        if (row.empty()) continue;
        if (readings.size() >= static_cast<std::size_t>(Constant::MaxSamplingPoints)) return false;
        const std::size_t new_size = readings.size() + 1u;
        if (new_size > Constant::MaxInstantAiReconstructionCells / reconstruction_rows) return false;
        double planned = 0.0, actual = 0.0, voltage = 0.0;
        int succeeded = 0, error_code = 0;
        if (!split_exact(row, 5, fields) || !parse_double(fields[0], planned) ||
            !parse_double(fields[1], actual) || !parse_double(fields[2], voltage, true) ||
            !parse_int(fields[3], succeeded) || (succeeded != 0 && succeeded != 1) ||
            !parse_int(fields[4], error_code) || planned < 0.0 || actual < 0.0 ||
            planned > loaded_config.instant_ai_planned_duration_seconds || actual > actual_duration ||
            (succeeded && (!std::isfinite(voltage) || error_code != 0)) ||
            (!succeeded && (!std::isnan(voltage) || error_code == 0))) return false;
        readings.push_back({planned, actual, voltage, succeeded != 0, error_code});
    }
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
    if (!read_required_line(input, frequency_line) || !read_required_line(input, points_line) ||
        !read_required_line(input, waveforms_line) || !read_required_line(input, header)) return false;
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
    while (true) {
        const ReadStatus row_status = read_bounded_line(input, row, max_row_length);
        if (row_status == ReadStatus::End) break;
        if (row_status != ReadStatus::Record) return false;
        if (row.empty()) continue;
        if (voltages.size() >= static_cast<std::size_t>(Constant::MaxSamplingPoints)) return false;
        int waveform = 0;
        double planned = 0.0, actual = 0.0, voltage = 0.0;
        if (!split_exact(row, 4, fields) || !parse_int(fields[0], waveform) || waveform < 0 ||
            waveform >= waveforms || !parse_double(fields[1], planned) || planned < 0.0 ||
            !parse_double(fields[2], actual) || actual < 0.0 || !parse_double(fields[3], voltage)) return false;
        loaded_waveforms[static_cast<std::size_t>(waveform)].push_back({planned, actual, voltage});
        voltages.push_back(voltage);
    }
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
    if (per_sample > static_cast<std::size_t>(Constant::MaxSamplingPoints)) return false;
    const std::size_t capacity = buffered_capacity(config);
    const bool independent = config.waveform_processing_mode == "independent_cycle";
    const std::size_t confirmation_points = independent
        ? static_cast<std::size_t>(std::max(32, static_cast<int>(std::llround(config.waveform_length * 0.00005))))
        : 0u;
    const std::size_t input_limit = independent ? capacity + confirmation_points : per_sample;
    std::vector<double> values;
    values.reserve(std::min(capacity, static_cast<std::size_t>(4096)));
    std::string token;
    bool ended_by_delimiter = false;
    while (true) {
        const ReadStatus token_status = read_bounded_field(
            input, token, ',', max_buffered_token_length, ended_by_delimiter);
        if (token_status == ReadStatus::End) return false;
        if (token_status != ReadStatus::Record || values.size() >= input_limit) return false;
        double value = 0.0;
        if (!parse_double(trim_space(token), value)) return false;
        values.push_back(value);
        if (!ended_by_delimiter) break;
    }
    // Historical buffered files contain one acquisition block.  Independent-cycle
    // files may contain every RunOnce block so replay preserves batch boundaries.
    if (values.size() != per_sample &&
        (!independent || (values.size() != capacity && values.size() != input_limit))) return false;
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
    if (!valid_dump_source(config, result)) return fail_dump(result, Error::INVALID_INSTANT_AI_CONFIG);
    if (!path_supported(config.dump_file_path)) return fail_dump(result);
    const std::string temporary_path = unique_temporary_path(config.dump_file_path);
    if (temporary_path.empty()) return fail_dump(result);
    std::ofstream output;
    output.imbue(std::locale::classic());
    if (!open_output(temporary_path, output)) {
        remove_path(temporary_path);
        return fail_dump(result);
    }
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
        const std::size_t count = buffered_dump_count(config);
        for (std::size_t i = 0; i < count; ++i) {
            write_number(output, result.totalSamplingBuffer[i]);
            if (i + 1 < count) output << ',';
        }
    }
    output.flush();
    const bool write_succeeded = output.good();
    output.close();
    if (!write_succeeded || !output.good()) {
        remove_path(temporary_path);
        return fail_dump(result);
    }
    if (!replace_atomically(temporary_path, config.dump_file_path)) {
        remove_path(temporary_path);
        return fail_dump(result);
    }
    return true;
}

bool Sampler::load_origin_data(Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.dump_file_path.empty()) {
        set_load_error(result, Error::FILE_NOT_FOUND);
        return false;
    }
    if (!path_supported(config.dump_file_path)) {
        set_load_error(result, Error::FILE_NOT_FOUND);
        return false;
    }
    std::ifstream input;
    input.imbue(std::locale::classic());
    if (!open_input(config.dump_file_path, input)) {
        set_load_error(result, Error::FILE_NOT_FOUND);
        return false;
    }
    bool loaded = false;
    if (input.peek() == '#') {
        std::string first_line;
        if (read_bounded_line(input, first_line, max_marker_length) != ReadStatus::Record) {
            set_load_error(result);
            return false;
        }
        if (first_line == instant_v2_marker) loaded = load_v2(input, config, result);
        else if (first_line == instant_v1_marker) loaded = load_v1(input, config, result);
    } else if (input.peek() != std::char_traits<char>::eof()) {
        loaded = load_buffered(input, config, result);
    }
    if (!loaded) set_load_error(result);
    return loaded;
}
} // namespace Sampler
