#ifndef SAMPLER_HPP
#define SAMPLER_HPP

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include "../config/sampling_config.hpp"
#include "../result/sampling_result.hpp"

namespace Sampler {

    class Sampler {
    public:
        virtual bool sample(const Config::SamplingConfig &config, Result::SamplingResult &result) = 0;

        virtual std::string name() = 0;

        virtual double get_value(const std::string &key) = 0;

        virtual bool set_value(const std::string &key, double value) = 0;

        static bool dump_origin_data(const Config::SamplingConfig &config, Result::SamplingResult &result) {
            if (!config.dump_file_path.empty()) {
                std::ofstream oss(config.dump_file_path);
                if (!oss.is_open()) {
                    result.error_code = Error::Code::FILE_NOT_FOUND;
                    return false;
                }
                for (size_t i = 0; i < config.sampling_length; i++) {
                    oss << result.buffer[i];
                    if (i < config.sampling_length - 1) {
                        oss << ",";
                    }
                }
                oss.close();
            }
            return true;
        }

        static bool load_origin_data(const Config::SamplingConfig &config, Result::SamplingResult &result) {
            if (config.dump_file_path.empty()) {
                result.error_code = Error::Code::FILE_NOT_FOUND;
                return false;
            }
            std::ifstream iss(config.dump_file_path);
            if (!iss.is_open()) {
                result.error_code = Error::Code::FILE_NOT_FOUND;
                return false;
            }
            std::string data;
            size_t i = 0;
            while (std::getline(iss, data, ',')) {
                result.buffer[i++] = std::stod(data);
            }
            iss.close();
            return true;
        }
    };

    typedef std::shared_ptr<Sampler> SamplerPtr;

} // namespace Sampler

#endif
