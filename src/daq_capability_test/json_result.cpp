#include "daq_capability_test/json_result.hpp"

#include <iomanip>
#include <sstream>

namespace daq_capability_test {
namespace {

bool is_continuation(unsigned char value)
{
    return value >= 0x80 && value <= 0xbf;
}

unsigned int valid_utf8_length(const std::string& value, std::string::size_type index)
{
    const unsigned char first = static_cast<unsigned char>(value[index]);
    const std::string::size_type remaining = value.size() - index;
    if (first >= 0xc2 && first <= 0xdf && remaining >= 2
        && is_continuation(static_cast<unsigned char>(value[index + 1]))) {
        return 2;
    }
    if (first >= 0xe0 && first <= 0xef && remaining >= 3) {
        const unsigned char second = static_cast<unsigned char>(value[index + 1]);
        const bool valid_second = (first == 0xe0 && second >= 0xa0 && second <= 0xbf)
            || (first >= 0xe1 && first <= 0xec && is_continuation(second))
            || (first == 0xed && second >= 0x80 && second <= 0x9f)
            || (first >= 0xee && first <= 0xef && is_continuation(second));
        if (valid_second && is_continuation(static_cast<unsigned char>(value[index + 2]))) {
            return 3;
        }
    }
    if (first >= 0xf0 && first <= 0xf4 && remaining >= 4) {
        const unsigned char second = static_cast<unsigned char>(value[index + 1]);
        const bool valid_second = (first == 0xf0 && second >= 0x90 && second <= 0xbf)
            || (first >= 0xf1 && first <= 0xf3 && is_continuation(second))
            || (first == 0xf4 && second >= 0x80 && second <= 0x8f);
        if (valid_second
            && is_continuation(static_cast<unsigned char>(value[index + 2]))
            && is_continuation(static_cast<unsigned char>(value[index + 3]))) {
            return 4;
        }
    }
    return 0;
}

std::string escape_json(const std::string& value)
{
    std::ostringstream output;
    for (std::string::size_type index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character >= 0x80) {
            const unsigned int length = valid_utf8_length(value, index);
            if (length != 0) {
                output.write(value.data() + index, length);
                index += length - 1;
            } else {
                output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<unsigned int>(character) << std::dec;
            }
            continue;
        }
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(character) << std::dec;
            } else {
                output << value[index];
            }
        }
    }
    return output.str();
}

const char* status_name(Status status)
{
    switch (status) {
    case Status::Pass: return "PASS";
    case Status::Fail: return "FAIL";
    case Status::Skip: return "SKIP";
    }
    return "FAIL";
}

}  // namespace

std::string to_json(const CommandResult& result)
{
    std::ostringstream output;
    output << "{\"result\":\"" << status_name(result.status)
           << "\",\"code\":\"" << escape_json(result.code)
           << "\",\"message\":\"" << escape_json(result.message)
           << "\",\"evidence\":{";

    bool first = true;
    for (std::map<std::string, std::string>::const_iterator it = result.evidence.begin();
         it != result.evidence.end(); ++it) {
        if (!first) {
            output << ',';
        }
        first = false;
        output << '"' << escape_json(it->first) << "\":\"" << escape_json(it->second) << '"';
    }
    output << "}}";
    return output.str();
}

std::string json_string(const std::string& value)
{
    return std::string("\"") + escape_json(value) + "\"";
}

}  // namespace daq_capability_test
