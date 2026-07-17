#include "daq_capability_test/json_result.hpp"
#include "daq_capability_test/result_codes.hpp"

#include <cassert>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

using namespace daq_capability_test;

namespace {

CommandResult make_result(Status status, ExitCategory category) {
    CommandResult result;
    result.status = status;
    result.exit_category = category;
    result.code = "quoted\"code";
    result.message = "path\\part\nline\x01";
    result.evidence["z-key"] = "last";
    result.evidence["a-key"] = "first\tvalue";
    return result;
}

void test_json_is_escaped_and_deterministic() {
    const CommandResult result = make_result(Status::Pass, ExitCategory::Success);
    const std::string expected = "{\"result\":\"PASS\",\"code\":\"quoted\\\"code\","
                                 "\"message\":\"path\\\\part\\nline\\u0001\","
                                 "\"evidence\":{\"a-key\":\"first\\tvalue\",\"z-key\":\"last\"}}";
    assert(to_json(result) == expected);
    assert(to_json(result).find('\n') == std::string::npos);
}

std::string expected_control_escape(unsigned int value) {
    switch (value) {
    case 0x08:
        return "\\b";
    case 0x09:
        return "\\t";
    case 0x0a:
        return "\\n";
    case 0x0c:
        return "\\f";
    case 0x0d:
        return "\\r";
    default:
        std::ostringstream output;
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << value;
        return output.str();
    }
}

void test_json_escapes_every_control_byte() {
    for (unsigned int value = 0; value < 0x20; ++value) {
        CommandResult result = make_result(Status::Fail, ExitCategory::ValidationFailed);
        result.message.assign(1, static_cast<char>(value));
        const std::string expected = "\"message\":\"" + expected_control_escape(value) + "\"";
        assert(to_json(result).find(expected) != std::string::npos);
    }
}

std::string message_json(const std::string& message) {
    CommandResult result = make_result(Status::Fail, ExitCategory::ValidationFailed);
    result.message = message;
    return to_json(result);
}

void test_json_escapes_invalid_high_bytes() {
    const std::string invalid_bytes("\x80\xff", 2);
    assert(message_json(invalid_bytes).find("\"message\":\"\\u0080\\u00ff\"") != std::string::npos);
}

void test_json_preserves_valid_utf8() {
    const std::string chinese("\xe4\xb8\xad\xe6\x96\x87", 6);
    assert(message_json(chinese).find("\"message\":\"" + chinese + "\"") != std::string::npos);
}

void test_json_rejects_invalid_utf8_sequences() {
    struct Case {
        const char* bytes;
        unsigned int size;
        const char* escaped;
    };
    const Case cases[] = {{"\xc0\xaf", 2, "\\u00c0\\u00af"},
                          {"\xed\xa0\x80", 3, "\\u00ed\\u00a0\\u0080"},
                          {"\xf4\x90\x80\x80", 4, "\\u00f4\\u0090\\u0080\\u0080"},
                          {"\xe2\x82", 2, "\\u00e2\\u0082"},
                          {"\x80", 1, "\\u0080"}};
    for (unsigned int index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const std::string input(cases[index].bytes, cases[index].size);
        const std::string expected = "\"message\":\"" + std::string(cases[index].escaped) + "\"";
        assert(message_json(input).find(expected) != std::string::npos);
    }
}

void test_status_names() {
    assert(to_json(make_result(Status::Pass, ExitCategory::Success)).find("\"result\":\"PASS\"") != std::string::npos);
    assert(to_json(make_result(Status::Fail, ExitCategory::ValidationFailed)).find("\"result\":\"FAIL\"") !=
           std::string::npos);
    assert(to_json(make_result(Status::Skip, ExitCategory::Unsupported)).find("\"result\":\"SKIP\"") !=
           std::string::npos);
}

void test_exit_codes() {
    assert(exit_code(make_result(Status::Pass, ExitCategory::Success)) == 0);
    assert(exit_code(make_result(Status::Fail, ExitCategory::ValidationFailed)) == 2);
    assert(exit_code(make_result(Status::Skip, ExitCategory::Unsupported)) == 3);
    assert(exit_code(make_result(Status::Fail, ExitCategory::InvalidArguments)) == 4);
    assert(exit_code(make_result(Status::Fail, ExitCategory::Environment)) == 5);
    assert(exit_code(make_result(Status::Fail, ExitCategory::Driver)) == 6);
    assert(exit_code(make_result(Status::Fail, ExitCategory::Output)) == 7);
}

bool is_legal(Status status, ExitCategory category) {
    if (status == Status::Pass) {
        return category == ExitCategory::Success;
    }
    if (status == Status::Skip) {
        return category == ExitCategory::Unsupported;
    }
    return category != ExitCategory::Success && category != ExitCategory::Unsupported;
}

void test_default_result_is_a_validation_failure() {
    const CommandResult result;
    assert(result.status == Status::Fail);
    assert(result.exit_category == ExitCategory::ValidationFailed);
    assert(exit_code(result) == 2);
}

void test_illegal_status_category_combinations_are_invalid_arguments() {
    const Status statuses[] = {Status::Pass, Status::Fail, Status::Skip};
    const ExitCategory categories[] = {ExitCategory::Success,     ExitCategory::ValidationFailed,
                                       ExitCategory::Unsupported, ExitCategory::InvalidArguments,
                                       ExitCategory::Environment, ExitCategory::Driver,
                                       ExitCategory::Output};
    for (unsigned int status_index = 0; status_index < 3; ++status_index) {
        for (unsigned int category_index = 0; category_index < 7; ++category_index) {
            if (!is_legal(statuses[status_index], categories[category_index])) {
                assert(exit_code(make_result(statuses[status_index], categories[category_index])) == 4);
            }
        }
    }
}

} // namespace

void test_results() {
    test_json_is_escaped_and_deterministic();
    test_json_escapes_every_control_byte();
    test_json_escapes_invalid_high_bytes();
    test_json_preserves_valid_utf8();
    test_json_rejects_invalid_utf8_sequences();
    test_status_names();
    test_exit_codes();
    test_default_result_is_a_validation_failure();
    test_illegal_status_category_combinations_are_invalid_arguments();
}
