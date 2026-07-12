#pragma once

#include "daq_capability_test/daq_adapter.hpp"
#include "daq_capability_test/matrix.hpp"
#include "daq_capability_test/phase_stitcher.hpp"
#include "daq_capability_test/result_writer.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace daq_capability_test {

enum class SuiteScopeKind { All, Case, From };
struct SuiteScope {
    SuiteScopeKind kind;
    std::string case_name;
    static SuiteScope all() { return SuiteScope{SuiteScopeKind::All,std::string()}; }
    static SuiteScope single(const std::string& name) { return SuiteScope{SuiteScopeKind::Case,name}; }
    static SuiteScope from(const std::string& name) { return SuiteScope{SuiteScopeKind::From,name}; }
};

struct SuiteCaseResult { bool executed=false; CommandResult result; };
struct SuiteResult {
    std::map<std::string,SuiteCaseResult> cases;
    std::set<std::string> supported_strategies;
    std::map<std::string,std::string> rejected_strategies;
    CommandResult command;
    const SuiteCaseResult& case_result(const std::string& name) const;
};

class CaseExecutor {
public:
    virtual ~CaseExecutor() {}
    virtual CommandResult execute(const MatrixCase& matrix_case)=0;
};

std::string default_success_code(const std::string& case_name);
bool known_suite_case(const std::string& case_name);
SuiteResult run_suite(const Matrix& matrix, const SuiteScope& scope, CaseExecutor& executor);
SuiteResult run_suite(DaqAdapter& adapter, const Matrix& matrix, const SuiteScope& scope);
SuiteResult run_suite(DaqAdapter& adapter, const Matrix& matrix, const SuiteScope& scope, ResultWriter* writer);
SuiteResult run_suite(DaqAdapter& adapter, const Matrix& matrix, const SuiteScope& scope, ResultWriter* writer,
                      std::vector<SummaryRecord>* acquisition_summaries);
CommandResult run_matrix_case(DaqAdapter& adapter, const Matrix& matrix, const MatrixCase& matrix_case);
CommandResult reconstruct_phase_directory(const std::string& input_directory, const MatrixCase& matrix_case);
CommandResult reconstruct_phase_segments(const std::vector<Segment>& segments, const MatrixCase& matrix_case);
std::vector<AcquisitionRequest> phase_capture_requests(const MatrixCase& matrix_case);
CommandResult suite_command_result(const SuiteResult& result);
std::string suite_result_json(const SuiteResult& result);
SummaryRecord make_acquisition_summary(const MatrixCase& matrix_case, const AcquisitionRequest& request,
                                       unsigned int repetition, const AcquisitionData& data, bool trigger);

}  // namespace daq_capability_test
