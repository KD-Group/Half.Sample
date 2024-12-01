#ifndef ERROR_HPP
#define ERROR_HPP

#include <string>

namespace Error {

    enum Code {
        SUCCESS,

        // error of human
        COMMAND_NOT_FOUND,
        SAMPLER_NOT_FOUND,
        NOW_IN_MEASURING,
        FILE_NOT_FOUND,

        // error of sampling
        VOLTAGE_NOT_ENOUGH,
        REAL_SAMPLER_ERROR,
        WAVE_NOT_FOUND,
        APPROPRIATE_WAVE_NOT_FOUND,
    };

    std::string to_string(Code error_code);

} // namespace Error

#endif
