#include "daq_capability_test/result_writer.hpp"

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace daq_capability_test {

detail::DirectoryPlan detail::plan_directories(const std::string& path, PathFlavor flavor) {
    DirectoryPlan plan = {false, std::string(), std::vector<std::string>(), std::string()};
    const bool windows = flavor == PathFlavor::Windows;
    const auto separator = [windows](char value) { return value == '/' || (windows && value == '\\'); };
    if (path.empty()) { plan.error = "empty directory path"; return plan; }

    size_t position = 0;
    std::string current;
    if (windows && path.size() >= 2 && separator(path[0]) && separator(path[1])) {
        position = 2;
        while (position < path.size() && separator(path[position])) ++position;
        const size_t server_end = path.find_first_of("/\\", position);
        if (position == path.size() || server_end == std::string::npos) {
            plan.error = "incomplete UNC path"; return plan;
        }
        const std::string server = path.substr(position, server_end - position);
        position = server_end;
        while (position < path.size() && separator(path[position])) ++position;
        const size_t share_end = path.find_first_of("/\\", position);
        const std::string share = path.substr(position, share_end == std::string::npos
                                                ? std::string::npos : share_end - position);
        if (server.empty() || share.empty() || server == "." || server == ".." || share == "." || share == "..") {
            plan.error = "incomplete or unsafe UNC path"; return plan;
        }
        current = "//" + server + "/" + share;
        plan.root_path = current;
        position = share_end == std::string::npos ? path.size() : share_end + 1;
    } else if (windows && path.size() >= 2 && path[1] == ':') {
        const unsigned char drive = static_cast<unsigned char>(path[0]);
        if (!((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')) ||
            path.size() < 3 || !separator(path[2])) {
            plan.error = "drive path must be absolute"; return plan;
        }
        current = path.substr(0, 2) + "/";
        plan.root_path = current;
        position = 3;
    } else if (separator(path[0])) {
        current = "/";
        plan.root_path = current;
        position = 1;
    }

    while (position < path.size()) {
        while (position < path.size() && separator(path[position])) ++position;
        if (position == path.size()) break;
        size_t end = position;
        while (end < path.size() && !separator(path[end])) ++end;
        const std::string component = path.substr(position, end - position);
        if (component == "." || component == "..") {
            plan.error = "dot path components are not allowed"; return plan;
        }
        current = current.empty() ? component :
            ((current[current.size() - 1] == '/') ? current + component : current + "/" + component);
        plan.directories.push_back(current);
        position = end;
    }
    if (current.empty()) { plan.error = "empty directory path"; return plan; }
    if (plan.root_path.empty() && plan.directories.empty()) {
        plan.error = "empty directory path"; return plan;
    }
    plan.valid = true;
    return plan;
}

namespace {
std::atomic<unsigned long long> temporary_counter(0);

std::string join_path(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    const char last = left[left.size() - 1];
    const bool trailing_separator = last == '/'
#ifdef _WIN32
        || last == '\\'
#endif
        ;
    return trailing_separator ? left + right : left + "/" + right;
}
FsResult create_directory_chain(FileOperations& operations, const std::string& path, detail::PathFlavor flavor,
                                std::string& failed_path) {
    const detail::DirectoryPlan plan = detail::plan_directories(path, flavor);
    if (!plan.valid) { failed_path = path; return FsResult(false, false, 0, plan.error, "validation"); }
    if (plan.directories.empty()) {
        const FsResult present = operations.exists(plan.root_path);
        if (present.success && present.already_exists) return FsResult();
        failed_path = path;
        return present.success ? FsResult(false, false, 0, "root directory does not exist") : present;
    }
    for (size_t i = 0; i < plan.directories.size(); ++i) {
        const FsResult made = operations.create_directory(plan.directories[i]);
        if (!made.success) { failed_path = plan.directories[i]; return made; }
    }
    return FsResult();
}
bool safe_component(const std::string& value) {
    if (value.empty() || value == "." || value == "..") return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}
CommandResult success_result() {
    CommandResult result; result.status = Status::Pass; result.code = "OK";
    result.exit_category = ExitCategory::Success; return result;
}
std::string escape_tsv(const std::string& value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) switch (value[i]) {
        case '\\': result += "\\\\"; break; case '\t': result += "\\t"; break;
        case '\n': result += "\\n"; break; case '\r': result += "\\r"; break; default: result += value[i];
    }
    return result;
}
std::string error_text(int code) {
#ifdef _WIN32
    char buffer[32]; std::sprintf(buffer, "Windows error %d", code); return buffer;
#else
    return std::strerror(code);
#endif
}

#ifdef _WIN32
FsResult to_wide(const std::string& value, std::wstring& output) {
    if (value.empty()) { output.clear(); return FsResult(); }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), NULL, 0);
    if (!count) { int e = static_cast<int>(GetLastError()); return FsResult(false, false, e, error_text(e)); }
    output.assign(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &output[0], count)) {
        int e = static_cast<int>(GetLastError()); return FsResult(false, false, e, error_text(e));
    }
    return FsResult();
}
#endif

class NativeFileOperations : public FileOperations {
public:
    FsResult create_directory(const std::string& path) override {
#ifdef _WIN32
        std::wstring p; FsResult converted = to_wide(path, p); if (!converted.success) return converted;
        if (CreateDirectoryW(p.c_str(), NULL)) return FsResult();
        int e = static_cast<int>(GetLastError());
        if (e == ERROR_ALREADY_EXISTS) {
            const DWORD attributes = GetFileAttributesW(p.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
                return FsResult(true, true, e, error_text(e));
        }
        return FsResult(false, e == ERROR_ALREADY_EXISTS, e, error_text(e));
#else
        if (::mkdir(path.c_str(), 0777) == 0) return FsResult();
        int e = errno;
        if (e == EEXIST) { struct stat info; if (::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
            return FsResult(true, true, e, error_text(e)); }
        return FsResult(false, e == EEXIST, e, error_text(e));
#endif
    }
    FsResult exists(const std::string& path) override {
#ifdef _WIN32
        std::wstring p; FsResult converted = to_wide(path, p); if (!converted.success) return converted;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return FsResult(true, true);
        int e = static_cast<int>(GetLastError());
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return FsResult(true, false);
        return FsResult(false, false, e, error_text(e));
#else
        struct stat info; if (::stat(path.c_str(), &info) == 0) return FsResult(true, true);
        int e = errno; if (e == ENOENT) return FsResult(true, false); return FsResult(false, false, e, error_text(e));
#endif
    }
    FsResult write_exclusive(const std::string& path, const std::string& data) override {
#ifdef _WIN32
        std::wstring p; FsResult converted = to_wide(path, p); if (!converted.success) return converted;
        HANDLE file = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) { int e = static_cast<int>(GetLastError()); return FsResult(false, e == ERROR_FILE_EXISTS, e, error_text(e), "write"); }
        size_t offset = 0; while (offset < data.size()) { DWORD written = 0; DWORD amount = static_cast<DWORD>((data.size() - offset) > 0x7fffffff ? 0x7fffffff : data.size() - offset);
            if (!WriteFile(file, data.data() + offset, amount, &written, NULL) || written == 0) { int e = static_cast<int>(GetLastError()); CloseHandle(file); return FsResult(false, false, e, error_text(e), "write"); } offset += written; }
        if (!FlushFileBuffers(file)) { int e = static_cast<int>(GetLastError()); CloseHandle(file); return FsResult(false, false, e, error_text(e), "flush"); }
        if (!CloseHandle(file)) { int e = static_cast<int>(GetLastError()); return FsResult(false, false, e, error_text(e), "close"); }
        return FsResult();
#else
        int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) { int e = errno; return FsResult(false, e == EEXIST, e, error_text(e), "write"); }
        size_t offset = 0; while (offset < data.size()) { ssize_t n = ::write(fd, data.data() + offset, data.size() - offset); if (n < 0) { if (errno == EINTR) continue; int e = errno; ::close(fd); return FsResult(false, false, e, error_text(e), "write"); } offset += static_cast<size_t>(n); }
        if (::fsync(fd) != 0) { int e = errno; ::close(fd); return FsResult(false, false, e, error_text(e), "flush"); }
        if (::close(fd) != 0) { int e = errno; return FsResult(false, false, e, error_text(e), "close"); } return FsResult();
#endif
    }
    FsResult publish_no_replace(const std::string& temporary, const std::string& final) override {
#ifdef _WIN32
        std::wstring from, to; FsResult a = to_wide(temporary, from); if (!a.success) return a; FsResult b = to_wide(final, to); if (!b.success) return b;
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) return FsResult();
        int e = static_cast<int>(GetLastError()); return FsResult(false, e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS, e, error_text(e), "publish");
#else
        if (::link(temporary.c_str(), final.c_str()) != 0) { int e = errno; return FsResult(false, e == EEXIST, e, error_text(e), "publish"); }
        if (::unlink(temporary.c_str()) == 0) return FsResult();
        int publish_error = errno;
        return FsResult(false, false, publish_error, "OUTPUT_STATE_INDETERMINATE: published final retained; temp cleanup failed: " + error_text(publish_error), "publish");
#endif
    }
    FsResult remove(const std::string& path) override {
#ifdef _WIN32
        std::wstring p; FsResult converted = to_wide(path, p); if (!converted.success) return converted;
        if (DeleteFileW(p.c_str())) return FsResult(); int e = static_cast<int>(GetLastError());
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return FsResult(); return FsResult(false, false, e, error_text(e), "remove");
#else
        if (::unlink(path.c_str()) == 0 || errno == ENOENT) return FsResult(); int e = errno; return FsResult(false, false, e, error_text(e), "remove");
#endif
    }
};
unsigned long long process_id() {
#ifdef _WIN32
    return static_cast<unsigned long long>(_getpid());
#else
    return static_cast<unsigned long long>(getpid());
#endif
}
bool finite_capability(const CapabilityRecord& r) { return std::isfinite(r.min_sample_rate_hz) && std::isfinite(r.max_sample_rate_hz) && std::isfinite(r.trigger_delay_min) && std::isfinite(r.trigger_delay_max); }
bool finite_summary(const SummaryRecord& r) { return std::isfinite(r.requested_sample_rate_hz) && std::isfinite(r.actual_sample_rate_hz) && std::isfinite(r.expected_duration_seconds) && std::isfinite(r.measured_duration_seconds) && std::isfinite(r.trigger_delay) && std::isfinite(r.trigger_wait_seconds) && std::isfinite(r.wall_elapsed_seconds); }
}  // namespace

FsResult detail::ensure_directory_path(FileOperations& operations, const std::string& path, PathFlavor flavor,
                                       std::string& failed_path) {
    return create_directory_chain(operations, path, flavor, failed_path);
}

ResultWriter::ResultWriter(const std::string& root, Clock clock, std::shared_ptr<FileOperations> operations)
    : root_(root), clock_(clock), operations_(operations) { if (!operations_) operations_.reset(new NativeFileOperations()); }
CommandResult ResultWriter::failure(const std::string& path, const std::string& stage, const std::string& error, const std::string& code) const {
    CommandResult result; result.status = Status::Fail; result.code = code; result.message = "Unable to write output";
    result.exit_category = ExitCategory::Output; result.evidence["path"] = path; result.evidence["stage"] = stage; result.evidence["os_error"] = error; return result;
}
CommandResult ResultWriter::exception_failure() const { return failure(run_directory_, "exception", "unexpected exception"); }
CommandResult ResultWriter::create_run_directory() try {
    const std::string base = clock_(); if (!safe_component(base)) return failure(root_, "validation", "unsafe clock output", "INVALID_OUTPUT_PATH");
    std::string failed_path; FsResult root = detail::ensure_directory_path(*operations_, root_,
#ifdef _WIN32
                                                                           detail::PathFlavor::Windows,
#else
                                                                           detail::PathFlavor::Posix,
#endif
                                                                   failed_path);
    if (!root.success) return root.stage == "validation"
        ? failure(root_, "validation", root.message, "INVALID_OUTPUT_PATH")
        : failure(failed_path, "create_root", root.message);
    for (unsigned int suffix = 0; suffix < 10000; ++suffix) { std::ostringstream name; name << base; if (suffix) name << '-' << std::setw(3) << std::setfill('0') << suffix;
        const std::string candidate = join_path(root_, name.str()); FsResult present = operations_->exists(candidate); if (!present.success) return failure(candidate, "exists", present.message);
        if (present.already_exists) continue; FsResult made = operations_->create_directory(candidate); if (made.success && !made.already_exists) { run_directory_ = candidate; return success_result(); }
        if (!made.already_exists) return failure(candidate, "create_run_directory", made.message); }
    return failure(root_, "create_run_directory", "collision limit exhausted");
} catch (...) { return exception_failure(); }
CommandResult ResultWriter::atomic_write(const std::string& target, const std::string& data) {
    FsResult present = operations_->exists(target); if (!present.success) return failure(target, "exists", present.message); if (present.already_exists) return failure(target, "preflight", "target exists", "OUTPUT_ALREADY_EXISTS");
    std::ostringstream temp_name; temp_name << target << ".tmp." << process_id() << '.' << temporary_counter.fetch_add(1); const std::string temporary = temp_name.str();
    FsResult written = operations_->write_exclusive(temporary, data); if (!written.success) {
        FsResult cleanup = operations_->remove(temporary);
        if (!cleanup.success) {
            FsResult final_state = operations_->exists(target);
            CommandResult r = failure(target, "remove", cleanup.message, "OUTPUT_STATE_INDETERMINATE");
            r.evidence["primary_stage"] = written.stage.empty() ? "write" : written.stage;
            r.evidence["primary_error"] = written.message; r.evidence["cleanup_stage"] = "remove";
            r.evidence["cleanup_error"] = cleanup.message; r.evidence["temp_path"] = temporary;
            r.evidence["final_exists"] = final_state.success && final_state.already_exists ? "true" : "false"; return r;
        }
        return failure(target, written.stage.empty() ? "write" : written.stage, written.message,
                       written.already_exists ? "OUTPUT_ALREADY_EXISTS" : "OUTPUT_WRITE_FAILED");
    }
    FsResult published = operations_->publish_no_replace(temporary, target); if (published.success) return success_result();
    FsResult cleanup = operations_->remove(temporary); if (!cleanup.success) {
        FsResult final_state = operations_->exists(target);
        CommandResult r = failure(target, "remove", cleanup.message, "OUTPUT_STATE_INDETERMINATE");
        r.evidence["primary_stage"] = published.stage.empty() ? "publish" : published.stage;
        r.evidence["primary_error"] = published.message; r.evidence["cleanup_stage"] = "remove";
        r.evidence["cleanup_error"] = cleanup.message; r.evidence["temp_path"] = temporary;
        r.evidence["final_exists"] = final_state.success && final_state.already_exists ? "true" : "false"; return r;
    }
    if (published.already_exists) return failure(target, "publish", published.message, "OUTPUT_ALREADY_EXISTS");
    FsResult final_state = operations_->exists(target); if (!final_state.success) return failure(target, "exists", final_state.message);
    if (published.message.find("OUTPUT_STATE_INDETERMINATE:") == 0) { CommandResult r = failure(target, "publish", published.message, "OUTPUT_STATE_INDETERMINATE"); r.evidence["final_exists"] = "true"; return r; }
    if (final_state.already_exists) { CommandResult r = failure(target, "publish", published.message, "OUTPUT_STATE_INDETERMINATE"); r.evidence["final_exists"] = "true"; return r; }
    return failure(target, published.stage.empty() ? "publish" : published.stage, published.message);
}
#define REQUIRE_RUN() if (!initialized()) return failure("", "validation", "run directory not initialized", "RUN_DIRECTORY_NOT_INITIALIZED")
CommandResult ResultWriter::write_environment(const EnvironmentRecord& r) try { REQUIRE_RUN(); std::ostringstream o;
    o << "executable_variant\tbuild_id\tos_architecture\tprocess_architecture\truntime_path\truntime_version\tdevice_description\targuments\n" << escape_tsv(r.executable_variant) << '\t' << escape_tsv(r.build_id) << '\t' << escape_tsv(r.os_architecture) << '\t' << escape_tsv(r.process_architecture) << '\t' << escape_tsv(r.runtime_path) << '\t' << escape_tsv(r.runtime_version) << '\t' << escape_tsv(r.device_description) << '\t' << escape_tsv(r.arguments) << '\n'; return atomic_write(join_path(run_directory_, "environment.tsv"), o.str()); } catch (...) { return exception_failure(); }
CommandResult ResultWriter::write_capability(const CapabilityRecord& r) try { REQUIRE_RUN(); if (!finite_capability(r)) return failure(join_path(run_directory_, "capability.tsv"), "validation", "non-finite value"); std::ostringstream o; o.imbue(std::locale::classic()); o << std::setprecision(17);
    o << "device_description\tchannel_count\tmin_sample_rate_hz\tmax_sample_rate_hz\tmax_scan_count\tbuffer_capacity\ttrigger_supported\ttrigger_count\ttrigger_sources\ttrigger_actions\ttrigger_delay_min\ttrigger_delay_max\n" << escape_tsv(r.device_description) << '\t' << r.channel_count << '\t' << r.min_sample_rate_hz << '\t' << r.max_sample_rate_hz << '\t' << r.max_scan_count << '\t' << r.buffer_capacity << '\t' << (r.trigger_supported ? "true" : "false") << '\t' << r.trigger_count << '\t' << escape_tsv(r.trigger_sources) << '\t' << escape_tsv(r.trigger_actions) << '\t' << r.trigger_delay_min << '\t' << r.trigger_delay_max << '\n'; return atomic_write(join_path(run_directory_, "capability.tsv"), o.str()); } catch (...) { return exception_failure(); }
CommandResult ResultWriter::write_summary(const std::vector<SummaryRecord>& records) try { REQUIRE_RUN(); for (size_t i = 0; i < records.size(); ++i) if (!finite_summary(records[i])) return failure(join_path(run_directory_, "summary.tsv"), "validation", "non-finite value"); std::ostringstream o; o.imbue(std::locale::classic()); o << std::setprecision(17);
    o << "test_name\trepetition\trequested_sample_rate_hz\tactual_sample_rate_hz\tchannel_count\trequested_points_per_channel\tactual_points_per_channel\texpected_duration_seconds\tmeasured_duration_seconds\ttrigger_enabled\ttrigger_source\ttrigger_edge\ttrigger_action\ttrigger_delay\ttrigger_wait_seconds\twall_elapsed_seconds\ttimed_out\toverrun\tcache_overflow\tdriver_error_code\tdriver_error_stage\tstatus\tcode\tnote\n";
    for (size_t i = 0; i < records.size(); ++i) { const SummaryRecord& r = records[i]; o << escape_tsv(r.test_name) << '\t' << r.repetition << '\t' << r.requested_sample_rate_hz << '\t' << r.actual_sample_rate_hz << '\t' << r.channel_count << '\t' << r.requested_points_per_channel << '\t' << r.actual_points_per_channel << '\t' << r.expected_duration_seconds << '\t' << r.measured_duration_seconds << '\t' << (r.trigger_enabled ? "true" : "false") << '\t' << escape_tsv(r.trigger_source) << '\t' << escape_tsv(r.trigger_edge) << '\t' << escape_tsv(r.trigger_action) << '\t' << r.trigger_delay << '\t' << r.trigger_wait_seconds << '\t' << r.wall_elapsed_seconds << '\t' << (r.timed_out ? "true" : "false") << '\t' << (r.overrun ? "true" : "false") << '\t' << (r.cache_overflow ? "true" : "false") << '\t' << escape_tsv(r.driver_error_code) << '\t' << escape_tsv(r.driver_error_stage) << '\t' << escape_tsv(r.status) << '\t' << escape_tsv(r.code) << '\t' << escape_tsv(r.note) << '\n'; } return atomic_write(join_path(run_directory_, "summary.tsv"), o.str()); } catch (...) { return exception_failure(); }
CommandResult ResultWriter::write_log(const std::vector<std::string>& lines) try { REQUIRE_RUN(); std::ostringstream o; for (size_t i = 0; i < lines.size(); ++i) o << escape_tsv(lines[i]) << '\n'; return atomic_write(join_path(run_directory_, "test_log.txt"), o.str()); } catch (...) { return exception_failure(); }
CommandResult ResultWriter::write_raw(const std::string& test_name, unsigned int repetition, const std::vector<RawChannel>& channels) try { REQUIRE_RUN(); const std::string raw = join_path(run_directory_, "raw"); const std::string target = join_path(raw, test_name + "_" + std::to_string(repetition) + ".tsv"); if (test_name.empty() || test_name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) return failure(target, "validation", "unsafe test name", "INVALID_OUTPUT_PATH"); FsResult made = operations_->create_directory(raw); if (!made.success) return failure(raw, "create_raw_directory", made.message); if (channels.empty()) return failure(target, "validation", "no channels"); size_t count = channels[0].samples.size();
    for (size_t c = 0; c < channels.size(); ++c) { if (channels[c].samples.size() != count) return failure(target, "validation", "channel lengths differ"); for (size_t i = 0; i < count; ++i) if (!std::isfinite(channels[c].samples[i])) return failure(target, "validation", "non-finite sample"); }
    std::ostringstream o; o.imbue(std::locale::classic()); o << std::setprecision(17) << "sample_index"; for (size_t c = 0; c < channels.size(); ++c) o << '\t' << escape_tsv(channels[c].name); o << '\n'; for (size_t row = 0; row < count; ++row) { o << row; for (size_t c = 0; c < channels.size(); ++c) o << '\t' << channels[c].samples[row]; o << '\n'; } return atomic_write(target, o.str()); } catch (...) { return exception_failure(); }
  CommandResult ResultWriter::write_config_snapshot(const std::string& source, const std::string& filename) try { REQUIRE_RUN(); if (!safe_component(filename)) return failure(join_path(run_directory_, filename), "validation", "unsafe filename", "INVALID_OUTPUT_PATH"); std::ifstream input(source.c_str(), std::ios::binary); if (!input) return failure(source, "read_source", "open failed"); const std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()); if (input.bad()) return failure(source, "read_source", "read failed"); return atomic_write(join_path(run_directory_, filename), data); } catch (...) { return exception_failure(); }
  CommandResult ResultWriter::write_completion_marker() try { REQUIRE_RUN(); return atomic_write(join_path(run_directory_, "capture.complete"), "COMPLETE\n"); } catch (...) { return exception_failure(); }
#undef REQUIRE_RUN
}  // namespace daq_capability_test
