#include "daq_capability_test/cli.hpp"
#include "daq_capability_test/suite_runner.hpp"
#include "daq_capability_test/fake_daq_adapter.hpp"
#include "daq_capability_test/acquisition_runner.hpp"
#include "daq_capability_test/result_writer.hpp"

#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <fstream>
#include <iterator>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif

using namespace daq_capability_test;

namespace {
CommandResult pass(const std::string& code)
{
    CommandResult r; r.status=Status::Pass; r.exit_category=ExitCategory::Success; r.code=code; return r;
}

Matrix complete_matrix()
{
    const char* names[]={"preflight","device_capability","single_channel_boundary","low_sample_rate_500k",
        "low_sample_rate_200k","low_sample_rate_100k","dual_channel_reference","segment_phase","phase_stitch",
        "external_trigger","delay_trigger"};
    Matrix m;
    for (unsigned int i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        MatrixCase c; c.case_name=names[i]; c.enabled=true;
        if(c.case_name=="low_sample_rate_500k")c.sample_rate_hz=MatrixValue<double>::from_value(500000);
        if(c.case_name=="low_sample_rate_200k")c.sample_rate_hz=MatrixValue<double>::from_value(200000);
        if(c.case_name=="low_sample_rate_100k")c.sample_rate_hz=MatrixValue<double>::from_value(100000);
        m.cases.push_back(c);
    }
    return m;
}

class RecordingExecutor : public CaseExecutor {
public:
    std::map<std::string,CommandResult> results;
    std::vector<std::string> calls;
    CommandResult execute(const MatrixCase& c) override {
        calls.push_back(c.case_name);
        std::map<std::string,CommandResult>::const_iterator it=results.find(c.case_name);
        return it==results.end()?pass(default_success_code(c.case_name)):it->second;
    }
};
class ThrowingAdapter : public DaqAdapter {public:int stops=0;AdapterResult<CapabilityInfo> query_capabilities(const std::string&) override{AdapterResult<CapabilityInfo> r;r.success=true;return r;}AdapterResult<OperationInfo> configure(const AcquisitionRequest&) override{throw std::runtime_error("injected");}AdapterResult<AcquisitionData> acquire_once(const AcquisitionRequest&) override{return AdapterResult<AcquisitionData>();}AdapterResult<OperationInfo> configure_trigger(const TriggerRequest&) override{return AdapterResult<OperationInfo>();}void stop() noexcept override{++stops;}};
class AcquireThrowAdapter : public DaqAdapter {public:int stops=0;AdapterResult<CapabilityInfo> query_capabilities(const std::string&) override{AdapterResult<CapabilityInfo> r;r.success=true;return r;}AdapterResult<OperationInfo> configure(const AcquisitionRequest&) override{AdapterResult<OperationInfo> r;r.success=true;return r;}AdapterResult<AcquisitionData> acquire_once(const AcquisitionRequest&) override{throw std::runtime_error("acquire injected");}AdapterResult<OperationInfo> configure_trigger(const TriggerRequest&) override{AdapterResult<OperationInfo> r;r.success=true;return r;}void stop() noexcept override{++stops;}};
class QueryThrowAdapter : public AcquireThrowAdapter {public:AdapterResult<CapabilityInfo> query_capabilities(const std::string&) override{throw std::runtime_error("query injected");}};

void test_cli()
{
    const char* ok[]={"tool","suite","--config","m.tsv","--case","phase_stitch"};
    CliParseResult parsed=parse_cli(6,const_cast<char**>(ok));
    assert(parsed.ok() && parsed.options.command==CliCommand::Suite);
    assert(parsed.options.scope.kind==SuiteScopeKind::Case && parsed.options.scope.case_name=="phase_stitch");

    const char* conflict[]={"tool","suite","--config","m.tsv","--all","--from","segment_phase"};
    parsed=parse_cli(7,const_cast<char**>(conflict));
    assert(!parsed.ok() && parsed.result.code=="CONFLICTING_SCOPE");
    const char* unknown[]={"tool","suite","--config","m.tsv","--case","bogus"};
    parsed=parse_cli(6,const_cast<char**>(unknown));
    assert(!parsed.ok() && parsed.result.code=="UNKNOWN_CASE");

    const char* commands[][5]={{"tool","capability","--device","d",0},{"tool","acquire","--config","m",0},
        {"tool","trigger","--config","m"},{"tool","phase-stitch","capture","--config"},
        {"tool","phase-stitch","reconstruct","--input-dir"}};
    const CliCommand expected[]={CliCommand::Capability,CliCommand::Acquire,CliCommand::Trigger,
        CliCommand::PhaseCapture,CliCommand::PhaseReconstruct};
    const int counts[]={4,4,4,3,3};
    for(int i=0;i<5;++i) assert(parse_cli(counts[i],const_cast<char**>(commands[i])).options.command==expected[i]);

    const char* acquire[]={"tool","acquire","--channels","0,1","--range","-10V~10V","--rate","1000",
        "--points","20","--repeat","3","--timeout","1.5","--output-dir","out"};
    parsed=parse_cli(16,const_cast<char**>(acquire));
    assert(parsed.ok() && parsed.options.channels.size()==2 && parsed.options.repeat_count==3);
    assert(!parsed.options.jitter_provided);
    const char* phase_target[]={"tool","trigger","--channels","0,1","--range","-10V~10V","--rate","1000","--points","2000","--repeat","1","--timeout","1","--output-dir","out","--source","external_analog","--edge","rising","--action","delay_to_start","--delay","250","--target-phase-percent","25","--delay-tolerance","5"};
    parsed=parse_cli(28,const_cast<char**>(phase_target));assert(parsed.ok()&&parsed.options.target_phase_provided&&parsed.options.target_phase_percent==25&&parsed.options.delay_tolerance_provided);
    const char* bad_number[]={"tool","acquire","--channels","0","--rate","1x"};
    parsed=parse_cli(6,const_cast<char**>(bad_number)); assert(!parsed.ok() && parsed.result.code=="INVALID_OPTION_VALUE");
}

void test_dependencies_and_decisions()
{
    RecordingExecutor e;
    CommandResult bad; bad.status=Status::Fail; bad.code="REFERENCE_SIGNAL_INVALID";
    e.results["dual_channel_reference"]=bad;
    SuiteResult r=run_suite(complete_matrix(),SuiteScope::all(),e);
    assert(r.case_result("phase_stitch").result.status==Status::Skip);
    assert(r.case_result("phase_stitch").result.code=="PREREQUISITE_FAILED");
    assert(r.case_result("phase_stitch").result.evidence.at("prerequisite_code")=="REFERENCE_SIGNAL_INVALID");
    assert(r.case_result("external_trigger").executed);
    assert(r.rejected_strategies.at("PHASE_STITCHING")=="REFERENCE_SIGNAL_INVALID");

    RecordingExecutor success;
    SuiteResult good=run_suite(complete_matrix(),SuiteScope::all(),success);
    assert(good.supported_strategies.count("LOW_SAMPLE_RATE"));
    assert(good.supported_strategies.count("PHASE_STITCHING"));
    assert(good.supported_strategies.count("TRIGGER_DELAY"));

    RecordingExecutor partial;
    CommandResult rate_fail; rate_fail.status=Status::Fail; rate_fail.code="RATE_FAILED";
    partial.results["low_sample_rate_200k"]=rate_fail;
    partial.results["low_sample_rate_100k"]=rate_fail;
    SuiteResult partial_result=run_suite(complete_matrix(),SuiteScope::all(),partial);
    assert(partial_result.supported_strategies.count("LOW_SAMPLE_RATE"));
    assert(partial_result.case_result("low_sample_rate").result.evidence.at("passed_sample_rates_hz")=="500000");
    CommandResult audit=suite_command_result(r);
    assert(audit.evidence.count("supported_strategies"));assert(audit.evidence.count("rejected_strategies"));
    assert(audit.evidence.count("skipped_cases"));assert(audit.evidence.count("case.phase_stitch.message"));
    std::string audit_json=suite_result_json(r);assert(audit_json.find("\"supported_strategies\":[")!=std::string::npos);
    assert(audit_json.find("\"cases\":{")!=std::string::npos);
    SuiteResult escaped;escaped.command=pass("OK");escaped.rejected_strategies["bad\"key"]="slash\\\xff";
    std::string escaped_json=suite_result_json(escaped);assert(escaped_json.find("bad\\\"key")!=std::string::npos);
    assert(escaped_json.find("slash\\\\\\u00ff")!=std::string::npos);
}

void test_scope_and_capture_order()
{
    RecordingExecutor e;
    SuiteResult r=run_suite(complete_matrix(),SuiteScope::single("phase_stitch"),e);
    const char* expected[]={"preflight","device_capability","dual_channel_reference","segment_phase","phase_stitch"};
    assert(e.calls.size()==5);
    for(size_t i=0;i<5;++i) assert(e.calls[i]==expected[i]);

    RecordingExecutor from_executor;
    run_suite(complete_matrix(),SuiteScope::from("dual_channel_reference"),from_executor);
    assert(from_executor.calls.size()>=3);
    assert(from_executor.calls[0]=="preflight");
    assert(from_executor.calls[1]=="device_capability");
    assert(from_executor.calls[2]=="dual_channel_reference");

    MatrixCase c; c.device=MatrixValue<std::string>::from_value("d");
    c.sample_channel=MatrixValue<int>::from_value(0); c.reference_channel=MatrixValue<int>::from_value(1);
    c.value_range=MatrixValue<std::string>::from_value("-10V~10V");
    c.sample_rate_hz=MatrixValue<double>::from_value(1000.0); c.signal_frequency_hz=MatrixValue<double>::from_value(2.0);
    c.points_per_channel=MatrixValue<int>::from_value(10); c.timeout_seconds=MatrixValue<double>::from_value(1.0);
    c.max_attempts=MatrixValue<int>::from_value(3);
    std::vector<AcquisitionRequest> requests=phase_capture_requests(c);
    assert(requests.size()==3);
    assert(requests[0].sample_rate_hz<requests[1].sample_rate_hz);
    assert(requests[0].points_per_channel>=300);
    assert(requests[1].points_per_channel==10);
}

void test_cli_output_contract()
{
    FakeDaqAdapter adapter; const char* args[]={"tool","unknown"};
    std::ostringstream output,errors; std::streambuf* old_out=std::cout.rdbuf(output.rdbuf());
    std::streambuf* old_err=std::cerr.rdbuf(errors.rdbuf());
    int code=run_cli(adapter,2,const_cast<char**>(args)); std::cout.rdbuf(old_out); std::cerr.rdbuf(old_err);
    assert(code==4); assert(output.str().find("{\"result\":\"FAIL\"")==0);
    assert(output.str().empty()==false && output.str()[output.str().size()-1]=='\n');
    assert(errors.str().find("UNKNOWN_COMMAND")!=std::string::npos);
}

MatrixCase delay_case(int delay)
{MatrixCase c;c.case_name="delay_trigger";c.enabled=true;c.device=MatrixValue<std::string>::from_value("d");c.mode=MatrixValue<std::string>::from_value("delay_trigger");c.sample_channel=MatrixValue<int>::from_value(0);c.reference_channel=MatrixValue<int>::from_value(1);c.value_range=MatrixValue<std::string>::from_value("-10V~10V");c.sample_rate_hz=MatrixValue<double>::from_value(100000);c.signal_frequency_hz=MatrixValue<double>::from_value(1000);c.points_per_channel=MatrixValue<int>::from_value(30);c.repeat_count=MatrixValue<int>::from_value(1);c.timeout_seconds=MatrixValue<double>::from_value(1);c.trigger_source=MatrixValue<std::string>::from_value("external_analog");c.trigger_edge=MatrixValue<std::string>::from_value("rising");c.trigger_action=MatrixValue<std::string>::from_value("delay_to_start");c.trigger_delay_counts=MatrixValue<std::vector<int> >::from_value(std::vector<int>{delay,delay+250,delay+500,delay+750});c.trigger_delay_target_phase_percent=MatrixValue<std::vector<double> >::from_value(std::vector<double>{0,25,50,75});c.max_edge_jitter_us=MatrixValue<double>::from_value(20);c.max_delay_position_error_us=MatrixValue<double>::from_value(20);c.reference_duty_cycle_percent=MatrixValue<double>::from_value(50);c.max_duty_cycle_error_percent=MatrixValue<double>::from_value(2);return c;}

#ifdef _WIN32
AcquisitionData wave(unsigned int count,double rate,double frequency)
{
    AcquisitionData d;d.actual_sample_rate_hz=rate;d.duration_seconds=count/rate;d.channels.resize(2);
    for(unsigned int i=0;i<count;++i){double v=std::fmod(i*frequency/rate,1.0)<0.5?1.0:-1.0;d.channels[0].samples.push_back(v);d.channels[1].samples.push_back(v);}return d;
}
AcquisitionData edge_wave(unsigned int count,double rate,unsigned int edge)
{AcquisitionData d;d.actual_sample_rate_hz=rate;d.duration_seconds=count/rate;d.channels.resize(2);for(unsigned int i=0;i<count;++i){d.channels[0].samples.push_back(0);d.channels[1].samples.push_back(i<edge?0:1);}return d;}
unsigned int lines(const std::string& path){std::ifstream in(path.c_str());unsigned int count=0;std::string line;while(std::getline(in,line))++count;return count;}
std::string newest_dir(const std::string& root){_finddata_t found;intptr_t handle=_findfirst((root+"/*").c_str(),&found);if(handle==-1)return std::string();std::string run;do{if((found.attrib&_A_SUBDIR)&&std::string(found.name)!="."&&std::string(found.name)!=".."){std::string candidate=root+"/"+found.name;if(candidate>run)run=candidate;}}while(_findnext(handle,&found)==0);_findclose(handle);return run;}
void test_capture_offline_round_trip()
{
    _mkdir("cpp_build/task10_e2e");std::ifstream source("src/daq_capability_test/default_test_matrix.tsv",std::ios::binary);
    std::string matrix((std::istreambuf_iterator<char>(source)),std::istreambuf_iterator<char>());
    size_t phase_name=matrix.rfind("\tphase_stitch\t");size_t phase_attempts=matrix.rfind("REQUIRED",phase_name);assert(phase_attempts!=std::string::npos);matrix.replace(phase_attempts,8,"2");
    for(size_t p=0;(p=matrix.find("REQUIRED",p))!=std::string::npos;)matrix.replace(p,8,"1");
    for(size_t p=0;(p=matrix.find("1000000\t16000000",p))!=std::string::npos;)matrix.replace(p,16,"10000\t6");
    std::ofstream config("cpp_build/task10_e2e/matrix.tsv",std::ios::binary);config<<matrix;config.close();
    MatrixParseResult parsed_matrix=parse_matrix(matrix);assert(parsed_matrix.ok());const MatrixCase* phase=0;for(size_t i=0;i<parsed_matrix.matrix.cases.size();++i)if(parsed_matrix.matrix.cases[i].case_name=="phase_stitch")phase=&parsed_matrix.matrix.cases[i];assert(phase);
    assert(phase->points_per_channel.value()==6);std::vector<AcquisitionRequest> capture_requests=phase_capture_requests(*phase);assert(capture_requests[0].points_per_channel==31&&capture_requests[1].points_per_channel==6);
    AcquisitionData invalid_phase=wave(31,10000,1000);invalid_phase.timed_out=true;FakeDaqAdapter phase_timeout;phase_timeout.enqueue_success(invalid_phase);assert(run_matrix_case(phase_timeout,parsed_matrix.matrix,*phase).code=="TIMEOUT");assert(phase_timeout.stop_call_count()==1);invalid_phase=wave(31,10000,1000);invalid_phase.cache_overflow=true;FakeDaqAdapter phase_overflow;phase_overflow.enqueue_success(invalid_phase);assert(run_matrix_case(phase_overflow,parsed_matrix.matrix,*phase).code=="CACHE_OVERFLOW");assert(phase_overflow.stop_call_count()==1);FakeDaqAdapter phase_points;phase_points.enqueue_success(wave(30,10000,1000));assert(run_matrix_case(phase_points,parsed_matrix.matrix,*phase).code=="CHANNEL_POINT_COUNT_MISMATCH");assert(phase_points.stop_call_count()==1);
    ThrowingAdapter throwing;assert(run_matrix_case(throwing,parsed_matrix.matrix,*phase).code=="DRIVER_EXCEPTION");assert(throwing.stops==1);
    const char* throwing_capture[]={"tool","phase-stitch","capture","--config","cpp_build/task10_e2e/matrix.tsv","--output-dir","cpp_build/task10_phase_throw"};ThrowingAdapter cli_throwing;assert(run_cli(cli_throwing,7,const_cast<char**>(throwing_capture))==6);assert(cli_throwing.stops==1);std::string throwing_run=newest_dir("cpp_build/task10_phase_throw");assert(!std::ifstream(throwing_run+"/capture.complete").good());AcquireThrowAdapter acquire_throwing;assert(run_cli(acquire_throwing,7,const_cast<char**>(throwing_capture))==6);assert(acquire_throwing.stops==1);
    _mkdir("cpp_build/task10_incomplete");assert(reconstruct_phase_directory("cpp_build/task10_incomplete",*phase).code=="RUN_INCOMPLETE");
    AcquisitionData calibration_data=wave(31,10000,1000),segment_data=wave(6,10000,1000);assert(validate_acquisition_data(calibration_data,capture_requests[0]).status==Status::Pass);assert(validate_acquisition_data(segment_data,capture_requests[1]).status==Status::Pass);
    FakeDaqAdapter adapter;adapter.enqueue_success(calibration_data);adapter.enqueue_success(segment_data);
    const char* capture[]={"tool","phase-stitch","capture","--config","cpp_build/task10_e2e/matrix.tsv","--output-dir","cpp_build/task10_e2e/out"};
    run_cli(adapter,7,const_cast<char**>(capture));
    std::string run=newest_dir("cpp_build/task10_e2e/out");assert(!run.empty());
    assert(std::ifstream(run+"/capture.complete").good());assert(lines(run+"/raw/phase_stitch_0.tsv")==32);assert(lines(run+"/raw/phase_stitch_1.tsv")==7);
    const char* reconstruct[]={"tool","phase-stitch","reconstruct","--input-dir",0,"--config","cpp_build/task10_e2e/matrix.tsv","--output-dir","cpp_build/task10_reconstruct"};reconstruct[4]=run.c_str();
    int offline=run_cli(adapter,9,const_cast<char**>(reconstruct));assert(offline==0);std::string reconstructed_run=newest_dir("cpp_build/task10_reconstruct");assert(std::ifstream(reconstructed_run+"/environment.tsv").good());assert(std::ifstream(reconstructed_run+"/summary.tsv").good());assert(std::ifstream(reconstructed_run+"/test_log.txt").good());assert(std::ifstream(reconstructed_run+"/matrix.tsv").good());

    FakeDaqAdapter reconstruction_failure;AcquisitionData bad_segment=segment_data;std::fill(bad_segment.channels[1].samples.begin(),bad_segment.channels[1].samples.end(),0.0);reconstruction_failure.enqueue_success(calibration_data);reconstruction_failure.enqueue_success(bad_segment);const char* bad_capture[]={"tool","phase-stitch","capture","--config","cpp_build/task10_e2e/matrix.tsv","--output-dir","cpp_build/task10_reconstruct_fail"};assert(run_cli(reconstruction_failure,7,const_cast<char**>(bad_capture))==2);std::string bad_run=newest_dir("cpp_build/task10_reconstruct_fail");assert(!std::ifstream(bad_run+"/capture.complete").good());std::ifstream bad_summary_file(bad_run+"/summary.tsv");std::string bad_summary((std::istreambuf_iterator<char>(bad_summary_file)),std::istreambuf_iterator<char>());assert(bad_summary.find("FAIL\tREFERENCE_SPAN_TOO_LOW")!=std::string::npos);

    const char* suite_args[]={"tool","suite","--config","cpp_build/task10_e2e/matrix.tsv","--case","preflight","--output-dir","cpp_build/task10_suite"};FakeDaqAdapter suite_adapter;assert(run_cli(suite_adapter,8,const_cast<char**>(suite_args))==0);std::string suite_run=newest_dir("cpp_build/task10_suite");assert(std::ifstream(suite_run+"/environment.tsv").good());assert(std::ifstream(suite_run+"/summary.tsv").good());assert(std::ifstream(suite_run+"/test_log.txt").good());assert(std::ifstream(suite_run+"/matrix.tsv").good());
    const char* suite_raw_args[]={"tool","suite","--config","cpp_build/task10_e2e/matrix.tsv","--case","single_channel_boundary","--output-dir","cpp_build/task10_suite_raw"};FakeDaqAdapter suite_raw;AcquisitionData single=wave(6,10000,1000);single.channels.resize(1);suite_raw.enqueue_success(single);suite_raw.enqueue_success(single);suite_raw.enqueue_success(single);assert(run_cli(suite_raw,8,const_cast<char**>(suite_raw_args))==0);std::string suite_raw_run=newest_dir("cpp_build/task10_suite_raw");assert(std::ifstream(suite_raw_run+"/raw/single_channel_boundary_0.tsv").good());std::ifstream suite_summary_file(suite_raw_run+"/summary.tsv");std::string suite_summary((std::istreambuf_iterator<char>(suite_summary_file)),std::istreambuf_iterator<char>());assert(suite_summary.find("single_channel_boundary\t0\t10000\t10000\t1\t6\t6")!=std::string::npos);

    MatrixCase phase_case=*phase;phase_case.sample_rate_hz=MatrixValue<double>::from_value(1000000);std::vector<AcquisitionRequest> phase_requests=phase_capture_requests(phase_case);AcquisitionData waited=wave(301,100000,1000);waited.trigger_wait_seconds=4.5;waited.wall_elapsed_seconds=4.50301;SummaryRecord calibration_summary=make_acquisition_summary(phase_case,phase_requests[0],0,waited,false);SummaryRecord normal_summary=make_acquisition_summary(phase_case,phase_requests[1],1,wave(6,1000000,1000),false);assert(calibration_summary.requested_sample_rate_hz==100000&&calibration_summary.requested_points_per_channel==301&&calibration_summary.trigger_wait_seconds==4.5);assert(normal_summary.requested_sample_rate_hz==1000000&&normal_summary.requested_points_per_channel==6);

    FakeDaqAdapter partial;partial.enqueue_success(calibration_data);partial.enqueue_failure("INJECTED_FAILURE");
    const char* partial_capture[]={"tool","phase-stitch","capture","--config","cpp_build/task10_e2e/matrix.tsv","--output-dir","cpp_build/task10_partial"};assert(run_cli(partial,7,const_cast<char**>(partial_capture))==6);
    std::string partial_run=newest_dir("cpp_build/task10_partial");assert(!partial_run.empty());assert(!std::ifstream(partial_run+"/capture.complete").good());assert(reconstruct_phase_directory(partial_run,*phase).code=="RUN_INCOMPLETE");
}

void test_direct_acquire_and_trigger_parameters()
{
    _mkdir("cpp_build/task10_direct");FakeDaqAdapter acquire_adapter;CapabilityInfo direct_metadata;direct_metadata.runtime_path="C:/runtime.dll";direct_metadata.runtime_version="9.1";acquire_adapter.enqueue_query_success(direct_metadata);AcquisitionData data=wave(20,1000,10);data.channels.resize(1);acquire_adapter.enqueue_success(data);
    const char* acquire[]={"tool","acquire","--channels","0","--range","-10V~10V","--rate","1000","--points","20","--repeat","1","--timeout","1","--output-dir","cpp_build/task10_direct"};
    assert(run_cli(acquire_adapter,16,const_cast<char**>(acquire))==0);assert(acquire_adapter.acquisition_requests()[0].points_per_channel==20);std::string complete_run=newest_dir("cpp_build/task10_direct");assert(std::ifstream(complete_run+"/capture.complete").good());std::ifstream environment_file(complete_run+"/environment.tsv");std::string environment((std::istreambuf_iterator<char>(environment_file)),std::istreambuf_iterator<char>());assert(environment.find("tool acquire --channels")!=std::string::npos);assert(environment.find("x64")!=std::string::npos);assert(environment.find("C:/runtime.dll")!=std::string::npos);assert(environment.find("9.1")!=std::string::npos);assert(std::ifstream(complete_run+"/test_log.txt").good());assert(std::ifstream(complete_run+"/summary.tsv").good());assert(std::ifstream(complete_run+"/raw/acquire_0.tsv").good());
    FakeDaqAdapter trigger_adapter;trigger_adapter.enqueue_success(edge_wave(20,1000,4));
    const char* trigger[]={"tool","trigger","--channels","0,1","--range","-10V~10V","--rate","1000","--points","20","--repeat","1","--timeout","1","--output-dir","cpp_build/task10_direct","--source","external_analog","--edge","rising","--action","delay_to_start","--delay","4","--jitter","2","--delay-tolerance","2"};
    assert(run_cli(trigger_adapter,28,const_cast<char**>(trigger))==0);assert(trigger_adapter.trigger_requests()[0].delay_counts==4);
    FakeDaqAdapter optional_delay;optional_delay.enqueue_success(edge_wave(20,1000,8));assert(run_cli(optional_delay,26,const_cast<char**>(trigger))==0);
    FakeDaqAdapter bounded_delay;bounded_delay.enqueue_success(edge_wave(20,1000,8));assert(run_cli(bounded_delay,28,const_cast<char**>(trigger))==0);
    FakeDaqAdapter trigger_config_failure;trigger_config_failure.enqueue_trigger_failure("TRIGGER_CONFIG_FAILED");assert(run_cli(trigger_config_failure,28,const_cast<char**>(trigger))==6);assert(trigger_config_failure.stop_call_count()==1);
    FakeDaqAdapter unbounded_jitter;unbounded_jitter.enqueue_success(edge_wave(20,1000,4));unbounded_jitter.enqueue_success(edge_wave(20,1000,8));const char* no_jitter[]={"tool","trigger","--channels","0,1","--range","-10V~10V","--rate","1000","--points","20","--repeat","2","--timeout","1","--output-dir","cpp_build/task10_direct","--source","external_analog","--edge","rising","--action","none"};assert(run_cli(unbounded_jitter,22,const_cast<char**>(no_jitter))==0);

    FakeDaqAdapter timeout;AcquisitionData timed=edge_wave(20,1000,4);timed.timed_out=true;timeout.enqueue_success(timed);
    assert(run_cli(timeout,16,const_cast<char**>(acquire))==6);
    FakeDaqAdapter repeat_failure;repeat_failure.enqueue_success(data);repeat_failure.enqueue_success(timed);
    const char* repeated[]={"tool","acquire","--channels","0","--range","-10V~10V","--rate","1000","--points","20","--repeat","2","--timeout","1","--output-dir","cpp_build/task10_repeat_fail"};assert(run_cli(repeat_failure,16,const_cast<char**>(repeated))==6);std::string failed_run=newest_dir("cpp_build/task10_repeat_fail");assert(!std::ifstream(failed_run+"/capture.complete").good());std::ifstream summary_file(failed_run+"/summary.tsv");std::string summary((std::istreambuf_iterator<char>(summary_file)),std::istreambuf_iterator<char>());assert(summary.find("FAIL\tTIMEOUT")!=std::string::npos);

    Matrix matrix;MatrixCase mismatch_case=delay_case(0);mismatch_case.trigger_delay_target_phase_percent=MatrixValue<std::vector<double> >::from_value(std::vector<double>{10,35,60,85});matrix.cases.push_back(mismatch_case);FakeDaqAdapter mismatch;
    assert(run_matrix_case(mismatch,matrix,mismatch_case).code=="DELAY_POSITION_MISMATCH");
    MatrixCase incomplete_case=delay_case(0);incomplete_case.mock_scenario=MatrixValue<std::string>::from_value("long_trigger_wait");incomplete_case.trigger_delay_counts=MatrixValue<std::vector<int> >::from_value(std::vector<int>{0,250});incomplete_case.trigger_delay_target_phase_percent=MatrixValue<std::vector<double> >::from_value(std::vector<double>{0,25});FakeDaqAdapter incomplete;
    assert(run_matrix_case(incomplete,matrix,incomplete_case).code=="DELAY_WINDOW_INCOMPLETE");
    MatrixCase falling=delay_case(0);falling.trigger_edge=MatrixValue<std::string>::from_value("falling");FakeDaqAdapter falling_adapter;assert(run_matrix_case(falling_adapter,matrix,falling).code=="DELAY_TRIGGER_WINDOW_COVERED");
    MatrixCase uneven=delay_case(0);uneven.repeat_count=MatrixValue<int>::from_value(2);uneven.trigger_delay_counts=MatrixValue<std::vector<int> >::from_value(std::vector<int>{0,200,500,700});uneven.trigger_delay_target_phase_percent=MatrixValue<std::vector<double> >::from_value(std::vector<double>{0,20,50,70});uneven.max_edge_jitter_us=MatrixValue<double>::from_value(1);FakeDaqAdapter uneven_adapter;assert(run_matrix_case(uneven_adapter,matrix,uneven).code=="DELAY_TRIGGER_WINDOW_COVERED");
    MatrixCase duty30=delay_case(0);duty30.mock_scenario=MatrixValue<std::string>::from_value("duty_30");duty30.reference_duty_cycle_percent=MatrixValue<double>::from_value(30);FakeDaqAdapter duty_adapter;assert(run_matrix_case(duty_adapter,matrix,duty30).code=="DELAY_TRIGGER_WINDOW_COVERED");
    MatrixCase wrong_duty=duty30;wrong_duty.reference_duty_cycle_percent=MatrixValue<double>::from_value(50);FakeDaqAdapter wrong_duty_adapter;assert(run_matrix_case(wrong_duty_adapter,matrix,wrong_duty).code=="REFERENCE_DUTY_CYCLE_MISMATCH");
    MatrixCase drift=delay_case(0);drift.repeat_count=MatrixValue<int>::from_value(2);drift.mock_scenario=MatrixValue<std::string>::from_value("delay_repeat_drift");drift.max_edge_jitter_us=MatrixValue<double>::from_value(5);drift.max_delay_position_error_us=MatrixValue<double>::from_value(50);FakeDaqAdapter drift_adapter;assert(run_matrix_case(drift_adapter,matrix,drift).code=="TRIGGER_START_JITTER_EXCEEDED");

    FakeDaqAdapter capability;CapabilityInfo info;info.max_channels=4;info.min_sample_rate_hz=10;info.max_sample_rate_hz=1000;info.max_points_per_channel=20;info.buffer_capacity=80;info.supports_trigger=true;info.runtime_version="1\"2";capability.enqueue_query_success(info);
    const char* capability_args[]={"tool","capability","--output-dir","cpp_build/task10_capability"};assert(run_cli(capability,4,const_cast<char**>(capability_args))==0);
    std::string capability_run=newest_dir("cpp_build/task10_capability");assert(std::ifstream(capability_run+"/capability.tsv").good());std::ifstream capability_environment_file(capability_run+"/environment.tsv");std::string capability_environment((std::istreambuf_iterator<char>(capability_environment_file)),std::istreambuf_iterator<char>());assert(capability_environment.find("1\"2")!=std::string::npos);assert(capability_environment.find("tool capability --output-dir")!=std::string::npos);
    QueryThrowAdapter query_throwing;const char* query_args[]={"tool","capability","--output-dir","cpp_build/task10_query_throw"};assert(run_cli(query_throwing,4,const_cast<char**>(query_args))==6);std::string query_run=newest_dir("cpp_build/task10_query_throw");assert(std::ifstream(query_run+"/summary.tsv").good());assert(!std::ifstream(query_run+"/capture.complete").good());
}
#endif
}

void test_suite()
{
    test_cli();
    test_dependencies_and_decisions();
    test_scope_and_capture_order();
    test_cli_output_contract();
#ifdef _WIN32
    test_capture_offline_round_trip();
    test_direct_acquire_and_trigger_parameters();
#endif
}
