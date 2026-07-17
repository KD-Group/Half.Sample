#ifndef SAMPLER_HPP
#define SAMPLER_HPP

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <memory>
#include <string>
#include "../config/sampling_config.hpp"
#include "../result/sampling_result.hpp"

namespace Sampler {

class Sampler {
  public:
    virtual bool sample(const Config::SamplingConfig& config, Result::SamplingResult& result) = 0;

    virtual std::string name() = 0;

    virtual double get_value(const std::string& key) = 0;

    virtual bool set_value(const std::string& key, double value) = 0;

    static bool dump_origin_data(const Config::SamplingConfig& config, Result::SamplingResult& result) {
        if (!config.dump_file_path.empty()) {
            std::ofstream oss(config.dump_file_path);
            if (!oss.is_open()) {
                result.error_code = Error::Code::FILE_NOT_FOUND;
                return false;
            }
            if (config.is_instant()) {
                oss << "#HALF_SAMPLE_INSTANT_AI_V1\n";
                oss << "emitting_frequency=" << std::setprecision(17) << config.emitting_frequency << "\n";
                oss << "target_points=" << config.instant_ai_target_points_per_waveform << "\n";
                oss << "number_of_waveforms=" << config.number_of_waveforms << "\n";
                oss << "waveform_index,planned_seconds,actual_seconds,voltage\n";
                for (std::size_t waveform = 0; waveform < result.instant_ai_waveforms.size(); ++waveform) {
                    for (const auto& reading : result.instant_ai_waveforms[waveform]) {
                        oss << waveform << "," << reading.planned_seconds << "," << reading.actual_seconds << ","
                            << reading.voltage << "\n";
                    }
                }
                return true;
            }
            for (size_t i = 0; i < config.sampling_length_per_sample; i++) {
                oss << result.totalSamplingBuffer[i];
                if (i < config.sampling_length_per_sample - 1) {
                    oss << ",";
                }
            }
            oss.close();
        }
        return true;
    }

    static bool load_origin_data(Config::SamplingConfig& config, Result::SamplingResult& result) {
        if (config.dump_file_path.empty()) {
            result.error_code = Error::Code::FILE_NOT_FOUND;
            return false;
        }
        std::ifstream iss(config.dump_file_path);
        if (!iss.is_open()) {
            result.error_code = Error::Code::FILE_NOT_FOUND;
            return false;
        }
        std::string first_line;
        std::getline(iss, first_line);
        if (first_line == "#HALF_SAMPLE_INSTANT_AI_V1") {
            std::string frequency_line;
            std::string points_line;
            std::string waveforms_line;
            std::string header;
            if (!std::getline(iss, frequency_line) || !std::getline(iss, points_line) ||
                !std::getline(iss, waveforms_line) || !std::getline(iss, header)) {
                result.error_code = Error::INVALID_INSTANT_AI_CONFIG;
                return false;
            }
            const double frequency = std::stod(frequency_line.substr(frequency_line.find('=') + 1));
            const int points = std::stoi(points_line.substr(points_line.find('=') + 1));
            const int waveforms = std::stoi(waveforms_line.substr(waveforms_line.find('=') + 1));
            if (header != "waveform_index,planned_seconds,actual_seconds,voltage" ||
                !config.update(waveforms, frequency, frequency, points)) {
                result.error_code = Error::INVALID_INSTANT_AI_CONFIG;
                return false;
            }
            result.instant_ai_waveforms.assign(static_cast<std::size_t>(waveforms), InstantAi::TimedWaveform());
            result.totalSamplingBuffer.clear();
            std::string row;
            while (std::getline(iss, row)) {
                if (row.empty())
                    continue;
                std::istringstream parser(row);
                std::string field;
                std::getline(parser, field, ',');
                const int waveform = std::stoi(field);
                std::getline(parser, field, ',');
                const double planned = std::stod(field);
                std::getline(parser, field, ',');
                const double actual = std::stod(field);
                std::getline(parser, field, ',');
                const double voltage = std::stod(field);
                if (waveform < 0 || waveform >= waveforms) {
                    result.error_code = Error::INVALID_INSTANT_AI_CONFIG;
                    return false;
                }
                result.instant_ai_waveforms[static_cast<std::size_t>(waveform)].push_back({planned, actual, voltage});
                result.totalSamplingBuffer.push_back(voltage);
            }
            result.resultWave.resize(config.valid_length);
            return true;
        }
        iss.clear();
        iss.seekg(0);
        std::string data;
        size_t i = 0;
        while (std::getline(iss, data, ',')) {
            result.totalSamplingBuffer[i++] = std::stod(data);
        }
        iss.close();
        return true;
    }
};

typedef std::shared_ptr<Sampler> SamplerPtr;

} // namespace Sampler

#endif
