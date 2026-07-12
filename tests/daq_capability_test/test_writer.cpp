#include "daq_capability_test/result_writer.hpp"
#include "daq_capability_test/result_codes.hpp"

#include <atomic>
#include <cassert>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace daq_capability_test;

namespace {
std::atomic<unsigned int> next_id(0);
int process_id() {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}
std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a[a.size() - 1];
    return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}
#ifdef _WIN32
std::wstring wide(const std::string& value) {
    if (value.empty()) return std::wstring();
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                    static_cast<int>(value.size()), NULL, 0);
    assert(count > 0);
    std::wstring result(static_cast<size_t>(count), L'\0');
    assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                               static_cast<int>(value.size()), &result[0], count) == count);
    return result;
}
bool exists(const std::string& path) { return GetFileAttributesW(wide(path).c_str()) != INVALID_FILE_ATTRIBUTES; }
void make_dir(const std::string& path) { assert(CreateDirectoryW(wide(path).c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS); }
void remove_tree_w(const std::wstring& path) {
    WIN32_FIND_DATAW data;
    const std::wstring pattern = path + L"/*";
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name(data.cFileName);
            if (name == L"." || name == L"..") continue;
            const std::wstring full = path + L"/" + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree_w(full);
            else DeleteFileW(full.c_str());
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(path.c_str());
}
void remove_tree(const std::string& path) { remove_tree_w(wide(path)); }
#else
bool exists(const std::string& path) { struct stat info; return ::stat(path.c_str(), &info) == 0; }
void make_dir(const std::string& path) { assert(::mkdir(path.c_str(), 0777) == 0 || errno == EEXIST); }
void remove_tree(const std::string& path) {
    DIR* directory = ::opendir(path.c_str());
    if (!directory) return;
    while (dirent* entry = ::readdir(directory)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        const std::string child = join(path, name);
        struct stat info;
        if (::lstat(child.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) remove_tree(child);
        else ::unlink(child.c_str());
    }
    ::closedir(directory);
    ::rmdir(path.c_str());
}
#endif
struct TempDir {
    std::string path;
    TempDir() : path(join("cpp_build", "writer-" + std::to_string(process_id()) + "-" +
                         std::to_string(next_id.fetch_add(1)))) { make_dir("cpp_build"); make_dir(path); }
    ~TempDir() { remove_tree(path); }
};
std::string bytes(const std::string& path) {
#ifdef _WIN32
    HANDLE file = CreateFileW(wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return std::string();
    std::string result; char buffer[4096]; DWORD count = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &count, NULL) && count) result.append(buffer, count);
    CloseHandle(file); return result;
#else
    std::ifstream input(path.c_str(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
#endif
}
void put(const std::string& path, const std::string& data) {
#ifdef _WIN32
    HANDLE file = CreateFileW(wide(path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE); DWORD written = 0;
    assert(WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, NULL) && written == data.size());
    assert(CloseHandle(file));
#else
    std::ofstream output(path.c_str(), std::ios::binary); output.write(data.data(), data.size()); assert(output.good());
#endif
}
std::vector<std::string> entries(const std::string& path) {
    std::vector<std::string> result;
#ifdef _WIN32
    WIN32_FIND_DATAW data; HANDLE find = FindFirstFileW((wide(path) + L"/*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) { do { if (data.cFileName[0] != L'.') {
        int n = WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, NULL, 0, NULL, NULL);
        std::string s(static_cast<size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, &s[0], n, NULL, NULL); s.resize(static_cast<size_t>(n - 1)); result.push_back(s);
    }} while (FindNextFileW(find, &data)); FindClose(find); }
#else
    DIR* directory = ::opendir(path.c_str()); if (!directory) return result;
    while (dirent* entry = ::readdir(directory)) { std::string s(entry->d_name); if (s != "." && s != "..") result.push_back(s); }
    ::closedir(directory);
#endif
    return result;
}
EnvironmentRecord environment() { return {"release", "abc", "x64", "x64", "C:\\daq\tdll", "1.2", "dev\n1", "--x\\y"}; }
CapabilityRecord capability() { return {"dev\t1", 2, 0.1, 1000.25, 2000, 4096, true, 2, "digital\nD0", "start\\stop", 0.125, 1.5}; }
SummaryRecord summary() { return {"rate\tname", 1, 1000.25, 999.5, 2, 100, 99, 0.1, 0.10005, true, "D0\n", "rising", "start\\x", 0.0, 1.0, 1.10005, false, false, false, "E\t1", "read", "PASS", "OK", "note\nline"}; }
size_t columns(const std::string& line) { size_t n = 1; for (size_t i = 0; i < line.size(); ++i) if (line[i] == '\t') ++n; return n; }

class FakeFileOperations : public FileOperations {
public:
    std::string fail;
    std::string fail_create_path;
    std::vector<std::string> created_directories;
    bool publish_creates_final;
    bool publish_leaves_temp;
    unsigned int remove_calls;
    FakeFileOperations() : publish_creates_final(false), publish_leaves_temp(false), remove_calls(0) {}
    FsResult create_directory(const std::string& path) override {
        created_directories.push_back(path);
        if (fail == "mkdir" || path == fail_create_path) return FsResult(false, false, 5, "mkdir failed");
        if (::exists(path)) return FsResult(true, true);
        make_dir(path); return FsResult();
    }
    FsResult exists(const std::string& path) override { return fail == "exists" ? FsResult(false, false, 5, "exists failed") : FsResult(true, ::exists(path), 0, ""); }
    FsResult write_exclusive(const std::string& path, const std::string& data) override {
        if (fail == "write" || fail == "write_remove") return FsResult(false, false, 5, "write failed", "write");
        if (fail == "flush") { put(path, data); return FsResult(false, false, 6, "flush failed", "flush"); }
        if (fail == "close") { put(path, data); return FsResult(false, false, 7, "close failed", "close"); }
        put(path, data); return FsResult();
    }
    FsResult publish_no_replace(const std::string& temporary, const std::string& final) override {
        if (publish_creates_final) put(final, bytes(temporary));
        if (fail == "publish" || fail == "publish_remove") return FsResult(false, false, 8, "publish failed", "publish");
        return FsResult(false, false, 5, "unused");
    }
    FsResult remove(const std::string& path) override { ++remove_calls; if (fail == "remove" || fail == "write_remove" || fail == "publish_remove") return FsResult(false, false, 9, "remove failed", "remove");
#ifdef _WIN32
        DeleteFileW(wide(path).c_str());
#else
        ::unlink(path.c_str());
#endif
        return FsResult(); }
private:
    FsResult answer(const std::string& operation) { return fail == operation ? FsResult(false, false, 5, operation + " failed") : FsResult(); }
};

class RecordingFileOperations : public FileOperations {
public:
    std::vector<std::string> create_calls;
    std::vector<std::string> exists_calls;
    FsResult create_directory(const std::string& path) override {
        create_calls.push_back(path); return FsResult(true, true);
    }
    FsResult exists(const std::string& path) override {
        exists_calls.push_back(path); return FsResult(true, true);
    }
    FsResult write_exclusive(const std::string&, const std::string&) override { return FsResult(); }
    FsResult publish_no_replace(const std::string&, const std::string&) override { return FsResult(); }
    FsResult remove(const std::string&) override { return FsResult(); }
};

void assert_output_failure(const CommandResult& result, const std::string& stage, const std::string& error) {
    assert(result.status == Status::Fail && exit_code(result) == 7);
    assert(result.evidence.at("stage") == stage);
    assert(result.evidence.at("os_error") == error);
    assert(result.evidence.count("path") == 1);
}

void test_requires_initialized_run_directory_and_safe_components() {
    TempDir temp;
    ResultWriter uninitialized(temp.path, [] { return "20260711-123456"; });
    CommandResult r = uninitialized.write_environment(environment());
    assert(r.code == "RUN_DIRECTORY_NOT_INITIALIZED" && !exists("environment.tsv"));
    for (const std::string bad : {"", ".", "..", "bad/name", "bad\\name", "C:bad", "bad name"}) {
        ResultWriter writer(temp.path, [bad] { return bad; });
        assert(writer.create_run_directory().code == "INVALID_OUTPUT_PATH");
    }
    ResultWriter writer(temp.path, [] { return "valid.clock-1"; });
    assert(writer.create_run_directory().status == Status::Pass);
    const std::string source = join(temp.path, "source.ini"); put(source, "x=1\n");
    for (const std::string bad : {"", ".", "..", "a/b", "a\\b", "a:b", "a b"})
        assert(writer.write_config_snapshot(source, bad).code == "INVALID_OUTPUT_PATH");
}

void test_unicode_root_and_exact_schemas() {
    TempDir temp; const std::string root = join(temp.path, "\xe4\xb8\xad\xe6\x96\x87-root"); make_dir(root);
    ResultWriter writer(root, [] { return "20260711-123456"; }); assert(writer.create_run_directory().status == Status::Pass);
    assert(writer.write_environment(environment()).status == Status::Pass);
    assert(writer.write_capability(capability()).status == Status::Pass);
    assert(writer.write_summary(std::vector<SummaryRecord>(1, summary())).status == Status::Pass);
    const std::string cap = bytes(join(writer.run_directory(), "capability.tsv"));
    const std::string sum = bytes(join(writer.run_directory(), "summary.tsv"));
    std::istringstream ci(cap), si(sum); std::string ch, cv, sh, sv; std::getline(ci, ch); std::getline(ci, cv); std::getline(si, sh); std::getline(si, sv);
    if (!(columns(ch) == 12 && columns(cv) == 12 && columns(sh) == 24 && columns(sv) == 24)) {
        std::fprintf(stderr, "columns: %llu %llu %llu %llu\n", static_cast<unsigned long long>(columns(ch)),
                     static_cast<unsigned long long>(columns(cv)), static_cast<unsigned long long>(columns(sh)),
                     static_cast<unsigned long long>(columns(sv)));
        assert(false);
    }
    assert(sh.find("trigger_wait_seconds\twall_elapsed_seconds")!=std::string::npos);
    assert(cv.find("dev\\t1") == 0 && cv.find("0.10000000000000001") != std::string::npos && cv.find("digital\\nD0") != std::string::npos);
    assert(sv.find("rate\\tname") == 0 && sv.find("note\\nline") != std::string::npos);
    assert(writer.write_log({"first", "second\tfield", "third\r\nline", "slash\\end"}).status == Status::Pass);
    assert(bytes(join(writer.run_directory(), "test_log.txt")) ==
           "first\nsecond\\tfield\nthird\\r\\nline\nslash\\\\end\n");
    assert(writer.write_raw("rate-test_1", 3, {{"v", {1, 2}}, {"i", {3, 4}}}).status == Status::Pass);
    assert(bytes(join(join(writer.run_directory(), "raw"), "rate-test_1_3.tsv")) ==
           "sample_index\tv\ti\n0\t1\t3\n1\t2\t4\n");
}

void test_nested_root_is_created_component_by_component() {
    TempDir temp;
    const std::string root = join(join(temp.path, "new-parent"), "new-child");
    ResultWriter writer(root, [] { return "nested-run"; });
    assert(writer.create_run_directory().status == Status::Pass);
    assert(exists(root));
    assert(exists(join(root, "nested-run")));
}

void test_directory_plans_have_platform_specific_root_semantics() {
    struct Example { const char* path; detail::PathFlavor flavor; std::vector<std::string> expected; };
    const Example examples[] = {
        {"C:\\alpha/beta", detail::PathFlavor::Windows, {"C:/alpha", "C:/alpha/beta"}},
        {"\\\\server/share\\alpha", detail::PathFlavor::Windows, {"//server/share/alpha"}},
        {"//first/second", detail::PathFlavor::Posix, {"/first", "/first/second"}},
        {"C:/alpha", detail::PathFlavor::Posix, {"C:", "C:/alpha"}},
        {"relative\\alpha/beta", detail::PathFlavor::Windows,
         {"relative", "relative/alpha", "relative/alpha/beta"}}
    };
    for (size_t i = 0; i < sizeof(examples) / sizeof(examples[0]); ++i) {
        RecordingFileOperations operations; std::string failed;
        const FsResult result = detail::ensure_directory_path(operations, examples[i].path, examples[i].flavor, failed);
        assert(result.success && operations.create_calls == examples[i].expected);
        assert(operations.exists_calls.empty());
    }
}

void test_root_only_paths_are_checked_but_not_created() {
    for (const std::pair<std::string, detail::PathFlavor>& example : {
             std::make_pair(std::string("C:/"), detail::PathFlavor::Windows),
             std::make_pair(std::string("\\\\server\\share"), detail::PathFlavor::Windows),
             std::make_pair(std::string("/"), detail::PathFlavor::Posix)}) {
        RecordingFileOperations operations; std::string failed;
        assert(detail::ensure_directory_path(operations, example.first, example.second, failed).success);
        assert(operations.create_calls.empty());
        assert(operations.exists_calls == std::vector<std::string>(1, detail::plan_directories(
            example.first, example.second).root_path));
    }
}

void test_unsafe_or_incomplete_paths_are_rejected_before_create() {
    struct Example { const char* path; detail::PathFlavor flavor; };
    const Example examples[] = {{"new/../root", detail::PathFlavor::Posix},
        {"new/./root", detail::PathFlavor::Posix}, {"C:\\new\\..\\root", detail::PathFlavor::Windows},
        {"C:", detail::PathFlavor::Windows}, {"\\\\server", detail::PathFlavor::Windows},
        {"\\\\server\\", detail::PathFlavor::Windows}};
    for (size_t i = 0; i < sizeof(examples) / sizeof(examples[0]); ++i) {
        RecordingFileOperations operations; std::string failed;
        const FsResult result = detail::ensure_directory_path(operations, examples[i].path, examples[i].flavor, failed);
        assert(!result.success && result.stage == "validation" && failed == examples[i].path);
        assert(operations.create_calls.empty() && operations.exists_calls.empty());
    }
    std::shared_ptr<FakeFileOperations> operations(new FakeFileOperations());
    ResultWriter writer("new/../root", [] { return "run"; }, operations);
    const CommandResult result = writer.create_run_directory();
    assert(result.code == "INVALID_OUTPUT_PATH" && result.evidence.at("stage") == "validation");
    assert(result.evidence.at("path") == "new/../root" && operations->created_directories.empty());
}

void test_existing_directory_chain_succeeds() {
    TempDir temp;
    const std::string parent = join(temp.path, "existing-parent");
    const std::string root = join(parent, "existing-child");
    make_dir(parent); make_dir(root);
    ResultWriter writer(root, [] { return "existing-run"; });
    assert(writer.create_run_directory().status == Status::Pass);
    assert(exists(join(root, "existing-run")));
}

void test_nested_root_reports_the_failed_component() {
    TempDir temp;
    std::shared_ptr<FakeFileOperations> ops(new FakeFileOperations());
    const std::string failed = join(temp.path, "blocked");
    const std::string root = join(failed, "not-created");
    ops->fail_create_path = failed;
    ResultWriter writer(root, [] { return "unreachable-run"; }, ops);
    const CommandResult result = writer.create_run_directory();
    assert(result.status == Status::Fail && result.code == "OUTPUT_WRITE_FAILED");
    assert(result.evidence.at("stage") == "create_root");
    assert(result.evidence.at("path") == failed);
    assert(result.evidence.at("os_error") == "mkdir failed");
    assert(!exists(root));
}

void test_file_in_directory_chain_is_not_treated_as_a_directory() {
    TempDir temp;
    const std::string blocked = join(temp.path, "existing-file");
    put(blocked, "keep");
    ResultWriter writer(join(blocked, "child"), [] { return "unreachable-run"; });
    const CommandResult result = writer.create_run_directory();
    assert(result.status == Status::Fail && result.code == "OUTPUT_WRITE_FAILED");
    assert(result.evidence.at("stage") == "create_root");
    assert(result.evidence.at("path") == blocked);
    assert(bytes(blocked) == "keep");
}

void test_config_snapshot_is_byte_exact_and_existing_final_is_preserved() {
    TempDir temp; ResultWriter writer(temp.path, [] { return "snapshot"; });
    assert(writer.create_run_directory().status == Status::Pass);
    const std::string source = join(temp.path, "source.ini");
    const std::string data = "# comment\r\nname=\xe4\xb8\xad\xe6\x96\x87\r\n";
    put(source, data);
    assert(writer.write_config_snapshot(source, "config.ini").status == Status::Pass);
    assert(bytes(join(writer.run_directory(), "config.ini")) == data);
    const std::string existing = join(writer.run_directory(), "summary.tsv");
    put(existing, "keep-me");
    CommandResult result = writer.write_summary(std::vector<SummaryRecord>(1, summary()));
    assert(result.code == "OUTPUT_ALREADY_EXISTS");
    assert(bytes(existing) == "keep-me");
}

void test_identical_timestamps_create_unique_run_directories() {
    TempDir temp;
    ResultWriter first(temp.path, [] { return "20260711-123456"; });
    ResultWriter second(temp.path, [] { return "20260711-123456"; });
    assert(first.create_run_directory().status == Status::Pass);
    assert(second.create_run_directory().status == Status::Pass);
    assert(first.run_directory() != second.run_directory());
    assert(second.run_directory() == join(temp.path, "20260711-123456-001"));
}

void test_raw_channel_length_mismatch_leaves_no_output() {
    TempDir temp; ResultWriter writer(temp.path, [] { return "raw-length-mismatch"; });
    assert(writer.create_run_directory().status == Status::Pass);
    const std::string raw_directory = join(writer.run_directory(), "raw");
    const std::string final = join(raw_directory, "mismatch_2.tsv");
    CommandResult result = writer.write_raw("mismatch", 2, {{"a", {1}}, {"b", {2, 3}}});
    assert(result.status == Status::Fail && result.code == "OUTPUT_WRITE_FAILED");
    assert(result.evidence.at("stage") == "validation");
    assert(!exists(final));
    const std::vector<std::string> names = entries(raw_directory);
    for (size_t i = 0; i < names.size(); ++i) assert(names[i].find(".tmp.") == std::string::npos);
}

void test_all_nonfinite_doubles_are_rejected() {
    typedef double CapabilityRecord::* CapabilityDouble;
    const CapabilityDouble capability_fields[] = {&CapabilityRecord::min_sample_rate_hz,
        &CapabilityRecord::max_sample_rate_hz, &CapabilityRecord::trigger_delay_min,
        &CapabilityRecord::trigger_delay_max};
    for (size_t i = 0; i < sizeof(capability_fields) / sizeof(capability_fields[0]); ++i) {
        TempDir temp; ResultWriter writer(temp.path, [i] { return "cap-" + std::to_string(i); });
        assert(writer.create_run_directory().status == Status::Pass);
        CapabilityRecord c = capability(); c.*capability_fields[i] = std::numeric_limits<double>::infinity();
        assert(writer.write_capability(c).status == Status::Fail);
    }
    typedef double SummaryRecord::* SummaryDouble;
    const SummaryDouble summary_fields[] = {&SummaryRecord::requested_sample_rate_hz,
        &SummaryRecord::actual_sample_rate_hz, &SummaryRecord::expected_duration_seconds,
        &SummaryRecord::measured_duration_seconds, &SummaryRecord::trigger_delay,
        &SummaryRecord::trigger_wait_seconds, &SummaryRecord::wall_elapsed_seconds};
    for (size_t i = 0; i < sizeof(summary_fields) / sizeof(summary_fields[0]); ++i) {
        TempDir temp; ResultWriter writer(temp.path, [i] { return "summary-" + std::to_string(i); });
        assert(writer.create_run_directory().status == Status::Pass);
        SummaryRecord s = summary(); s.*summary_fields[i] = std::numeric_limits<double>::quiet_NaN();
        assert(writer.write_summary(std::vector<SummaryRecord>(1, s)).status == Status::Fail);
    }
    TempDir temp; ResultWriter writer(temp.path, [] { return "raw-finite"; });
    assert(writer.create_run_directory().status == Status::Pass);
    assert(writer.write_raw("raw", 1, {{"a", {-std::numeric_limits<double>::infinity()}}}).status == Status::Fail);
}

void test_injected_failures_and_indeterminate_publish() {
    for (const std::string failure : {"mkdir", "exists"}) {
        TempDir temp; std::shared_ptr<FakeFileOperations> ops(new FakeFileOperations()); ops->fail = failure;
        ResultWriter writer(temp.path, [] { return "fake-run"; }, ops);
        CommandResult result = writer.create_run_directory();
        if (failure == "mkdir" || failure == "exists") { assert(result.status == Status::Fail); continue; }
        assert(result.status == Status::Pass); result = writer.write_environment(environment()); assert(result.status == Status::Fail);
    }
    for (const std::string stage : {"write", "flush", "close", "publish"}) {
        TempDir temp; std::shared_ptr<FakeFileOperations> ops(new FakeFileOperations()); ops->fail = stage;
        ResultWriter writer(temp.path, [stage] { return "failure-" + stage; }, ops);
        assert(writer.create_run_directory().status == Status::Pass);
        const std::string final = join(join(writer.run_directory(), "raw"), "failure_1.tsv");
        CommandResult result = writer.write_raw("failure", 1, {{"a", {1}}});
        assert_output_failure(result, stage, stage + " failed");
        assert(!exists(final));
        const std::vector<std::string> names = entries(join(writer.run_directory(), "raw"));
        for (size_t i = 0; i < names.size(); ++i) assert(names[i].find(".tmp.") == std::string::npos);
    }
    for (const std::string failure : {"write_remove", "publish_remove"}) {
        TempDir temp; std::shared_ptr<FakeFileOperations> ops(new FakeFileOperations()); ops->fail = failure;
        ResultWriter writer(temp.path, [failure] { return "cleanup-" + failure; }, ops);
        assert(writer.create_run_directory().status == Status::Pass);
        CommandResult result = writer.write_environment(environment());
        assert(result.code == "OUTPUT_STATE_INDETERMINATE" && exit_code(result) == 7);
        assert(result.evidence.at("primary_stage") == (failure == "write_remove" ? "write" : "publish"));
        assert(result.evidence.at("primary_error") == (failure == "write_remove" ? "write failed" : "publish failed"));
        assert(result.evidence.at("cleanup_stage") == "remove");
        assert(result.evidence.at("cleanup_error") == "remove failed");
        assert(result.evidence.count("temp_path") == 1);
        assert(result.evidence.at("final_exists") == "false");
    }
    TempDir temp; std::shared_ptr<FakeFileOperations> ops(new FakeFileOperations()); ops->publish_creates_final = true; ops->fail = "publish_remove";
    ResultWriter writer(temp.path, [] { return "rollback"; }, ops); assert(writer.create_run_directory().status == Status::Pass);
    CommandResult result = writer.write_environment(environment()); assert(result.code == "OUTPUT_STATE_INDETERMINATE");
    assert(result.evidence.at("final_exists") == "true");
    assert(bytes(join(writer.run_directory(), "environment.tsv")).find("release") != std::string::npos);
    assert(ops->remove_calls == 1);
}

void test_real_concurrent_publish_is_complete_and_leaves_no_temp() {
    TempDir temp; ResultWriter writer(temp.path, [] { return "race"; }); assert(writer.create_run_directory().status == Status::Pass);
    EnvironmentRecord a = environment(), b = environment(); a.build_id = std::string(100000, 'A'); b.build_id = std::string(100000, 'B');
    CommandResult ra, rb; std::thread ta([&] { ra = writer.write_environment(a); }); std::thread tb([&] { rb = writer.write_environment(b); }); ta.join(); tb.join();
    assert((ra.status == Status::Pass) != (rb.status == Status::Pass));
    const std::string final = bytes(join(writer.run_directory(), "environment.tsv"));
    assert(final.find(std::string(100000, 'A')) != std::string::npos || final.find(std::string(100000, 'B')) != std::string::npos);
    const std::vector<std::string> names = entries(writer.run_directory());
    for (size_t i = 0; i < names.size(); ++i) assert(names[i].find(".tmp.") == std::string::npos);
}
}

void test_writer() {
    test_requires_initialized_run_directory_and_safe_components();
    test_unicode_root_and_exact_schemas();
    test_nested_root_is_created_component_by_component();
    test_directory_plans_have_platform_specific_root_semantics();
    test_root_only_paths_are_checked_but_not_created();
    test_unsafe_or_incomplete_paths_are_rejected_before_create();
    test_existing_directory_chain_succeeds();
    test_nested_root_reports_the_failed_component();
    test_file_in_directory_chain_is_not_treated_as_a_directory();
    test_config_snapshot_is_byte_exact_and_existing_final_is_preserved();
    test_identical_timestamps_create_unique_run_directories();
    test_raw_channel_length_mismatch_leaves_no_output();
    test_all_nonfinite_doubles_are_rejected();
    test_injected_failures_and_indeterminate_publish();
    test_real_concurrent_publish_is_complete_and_leaves_no_temp();
}
