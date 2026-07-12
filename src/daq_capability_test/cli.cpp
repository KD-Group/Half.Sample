#include "daq_capability_test/cli.hpp"

#include "daq_capability_test/acquisition_runner.hpp"
#include "daq_capability_test/json_result.hpp"
#include "daq_capability_test/result_codes.hpp"
#include "daq_capability_test/result_writer.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <locale>
#include <ctime>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif

namespace daq_capability_test {
namespace {
thread_local const CliOptions* active_cli_options=0;
thread_local std::string active_runtime_path="unavailable",active_runtime_version="unavailable";
CommandResult make(Status status,const std::string& code,const std::string& message,ExitCategory category)
{ CommandResult r; r.status=status; r.code=code; r.message=message; r.exit_category=category; return r; }

bool option_value(int argc,char** argv,int& index,std::string& target,CommandResult& error)
{
    if(index+1>=argc) { error=make(Status::Fail,"MISSING_OPTION_VALUE","Option requires a value",ExitCategory::InvalidArguments); return false; }
    target=argv[++index]; return true;
}

template<typename T> bool numeric(const std::string& text,T& value)
{ std::istringstream in(text); in.imbue(std::locale::classic()); in>>std::noskipws>>value; return in && in.peek()==std::char_traits<char>::eof(); }
bool channels(const std::string& text,std::vector<int>& values)
{ std::istringstream in(text); std::string part; while(std::getline(in,part,',')){int v;if(!numeric(part,v)||v<0)return false;values.push_back(v);} return !values.empty()&&values.size()<=2; }
std::string clock_value()
{ std::time_t now=std::time(0); std::tm value; localtime_s(&value,&now); std::ostringstream out; out<<std::put_time(&value,"%Y%m%d-%H%M%S"); return out.str(); }
template<typename T> std::string text(T value){std::ostringstream out;out<<value;return out.str();}
std::string joined(const std::vector<std::string>& values){std::string out;for(size_t i=0;i<values.size();++i){if(i)out+=",";out+=values[i];}return out.empty()?"unavailable":out;}
std::string native_architecture(){SYSTEM_INFO info;GetNativeSystemInfo(&info);switch(info.wProcessorArchitecture){case PROCESSOR_ARCHITECTURE_AMD64:return "x64";case PROCESSOR_ARCHITECTURE_INTEL:return "x86";case PROCESSOR_ARCHITECTURE_ARM64:return "ARM64";case PROCESSOR_ARCHITECTURE_ARM:return "ARM";default:return "unavailable";}}

CommandResult initialize_run(ResultWriter& writer,const CliOptions& options,const std::string& runtime_path="unavailable",const std::string& runtime_version="unavailable")
{CommandResult made=writer.create_run_directory();if(made.status!=Status::Pass)return made;EnvironmentRecord e;e.executable_variant=options.executable_variant.empty()?"unavailable":options.executable_variant;e.build_id=std::string(__DATE__)+" "+__TIME__;e.os_architecture=native_architecture();e.process_architecture=sizeof(void*)==8?"x64":"x86";e.runtime_path=runtime_path.empty()?"unavailable":runtime_path;e.runtime_version=runtime_version.empty()?"unavailable":runtime_version;e.device_description=options.device.empty()?"unavailable":options.device;e.arguments=options.invocation.empty()?"unavailable":options.invocation;return writer.write_environment(e);}
CommandResult initialize_run(ResultWriter& writer,const std::string& label)
{if(active_cli_options)return initialize_run(writer,*active_cli_options,active_runtime_path,active_runtime_version);CliOptions options;options.invocation=label;options.executable_variant="unavailable";return initialize_run(writer,options);}
CommandResult finish_run(ResultWriter& writer,const std::vector<SummaryRecord>& summaries,const CommandResult& outcome,bool complete)
{CommandResult written=writer.write_summary(summaries);if(written.status!=Status::Pass)return written;std::vector<std::string> log(1,outcome.code+": "+outcome.message);written=writer.write_log(log);if(written.status!=Status::Pass)return written;if(complete&&outcome.status==Status::Pass){written=writer.write_completion_marker();if(written.status!=Status::Pass)return written;}return outcome;}
SummaryRecord summary_for(const std::string& name,const CommandResult& outcome)
{SummaryRecord s={};s.test_name=name;s.status=outcome.status==Status::Pass?"PASS":outcome.status==Status::Skip?"SKIP":"FAIL";s.code=outcome.code;s.note=outcome.message;return s;}

class CachedCapabilityAdapter : public DaqAdapter {
public:
    CachedCapabilityAdapter(DaqAdapter& inner,const std::string& device,const AdapterResult<CapabilityInfo>& cached):inner_(inner),device_(device),cached_(cached){}
    AdapterResult<CapabilityInfo> query_capabilities(const std::string& device) override {return device==device_?cached_:inner_.query_capabilities(device);}
    AdapterResult<OperationInfo> configure(const AcquisitionRequest& request) override{return inner_.configure(request);}
    AdapterResult<AcquisitionData> acquire_once(const AcquisitionRequest& request) override{return inner_.acquire_once(request);}
    AdapterResult<OperationInfo> configure_trigger(const TriggerRequest& request) override{return inner_.configure_trigger(request);}
    void stop() noexcept override{inner_.stop();}
private:DaqAdapter& inner_;std::string device_;AdapterResult<CapabilityInfo> cached_;
};
class NoThrowStopGuard {public:explicit NoThrowStopGuard(DaqAdapter& adapter):adapter_(adapter){}~NoThrowStopGuard()noexcept{adapter_.stop();}private:DaqAdapter& adapter_;};
AdapterResult<CapabilityInfo> safe_query(DaqAdapter& adapter,const std::string& device){try{return adapter.query_capabilities(device);}catch(const std::exception& error){AdapterResult<CapabilityInfo> failed;failed.code="DRIVER_EXCEPTION";failed.message=error.what();failed.stage="query_capabilities";return failed;}catch(...){AdapterResult<CapabilityInfo> failed;failed.code="DRIVER_EXCEPTION";failed.message="Unknown driver exception";failed.stage="query_capabilities";return failed;}}

CommandResult output_failure(const CommandResult& value) { return value; }

bool first_edge_seconds(const AcquisitionData& data,const CliOptions& o,double& seconds)
{
    if(data.channels.size()<2||data.channels[1].samples.size()<2)return false;const std::vector<double>& samples=data.channels[1].samples;
    const std::pair<std::vector<double>::const_iterator,std::vector<double>::const_iterator> bounds=std::minmax_element(samples.begin(),samples.end());
    const double threshold=(*bounds.first+*bounds.second)/2.0;if(*bounds.second-*bounds.first<=0)return false;
    for(size_t i=1;i<samples.size();++i){bool crossed=o.trigger_edge=="falling"?(samples[i-1]>threshold&&samples[i]<=threshold):(samples[i-1]<threshold&&samples[i]>=threshold);if(crossed){seconds=i/data.actual_sample_rate_hz;return true;}}
    return false;
}
bool measured_start_phase(const AcquisitionData& data,const CliOptions& o,double& percent,double& period)
{
    if(data.channels.size()<2||data.channels[1].samples.size()<3)return false;const std::vector<double>& s=data.channels[1].samples;
    const std::pair<std::vector<double>::const_iterator,std::vector<double>::const_iterator> mm=std::minmax_element(s.begin(),s.end());if(*mm.second<=*mm.first)return false;double threshold=(*mm.first+*mm.second)/2;std::vector<double> edges;
    for(size_t i=1;i<s.size();++i){bool crossed=o.trigger_edge=="falling"?(s[i-1]>threshold&&s[i]<=threshold):(s[i-1]<threshold&&s[i]>=threshold);if(crossed){double fraction=(threshold-s[i-1])/(s[i]-s[i-1]);edges.push_back((i-1+fraction)/data.actual_sample_rate_hz);}}
    if(edges.size()<2)return false;period=(edges.back()-edges.front())/(edges.size()-1);double phase=std::fmod(1.0-edges.front()/period,1.0);if(phase<0)phase+=1.0;percent=phase*100;return true;
}

CommandResult direct_acquire(DaqAdapter& adapter,const CliOptions& o,bool trigger)
{
    AdapterResult<CapabilityInfo> metadata=adapter.query_capabilities(o.device);ResultWriter writer(o.output_directory,clock_value); CommandResult made=initialize_run(writer,o,metadata.success?metadata.value.runtime_path:"unavailable",metadata.success?metadata.value.runtime_version:"unavailable"); if(made.status!=Status::Pass)return made;if(!metadata.success){CommandResult failed=make(metadata.unsupported?Status::Skip:Status::Fail,metadata.code,metadata.message,metadata.unsupported?ExitCategory::Unsupported:ExitCategory::Driver);std::vector<SummaryRecord> audit(1,summary_for(trigger?"trigger":"acquire",failed));return finish_run(writer,audit,failed,false);}
    AcquisitionRequest request; request.device=o.device; request.channels=o.channels; request.value_range=o.value_range;
    request.sample_rate_hz=o.sample_rate_hz; request.points_per_channel=o.points_per_channel; request.timeout_seconds=o.timeout_seconds;
    std::vector<SummaryRecord> summaries;
    const auto fail=[&](CommandResult failed){summaries.push_back(summary_for(trigger?"trigger":"acquire",failed));failed.evidence["run_directory"]=writer.run_directory();return finish_run(writer,summaries,failed,false);};
    std::vector<double> edge_times;
    double max_delay_error_us=0.0;
    try {
    for(unsigned int repetition=0;repetition<o.repeat_count;++repetition) {
        NoThrowStopGuard stop(adapter);
        AdapterResult<OperationInfo> configured=adapter.configure(request);
        if(!configured.success) return fail(make(Status::Fail,configured.code,configured.message,ExitCategory::Driver));
        if(trigger) { TriggerRequest t; t.source=o.trigger_source;t.edge=o.trigger_edge;t.action=o.trigger_action;t.delay_counts=o.trigger_delay_counts; AdapterResult<OperationInfo> tr=adapter.configure_trigger(t);if(!tr.success)return fail(make(Status::Fail,tr.code,tr.message,ExitCategory::Driver)); }
        AdapterResult<AcquisitionData> acquired=adapter.acquire_once(request);
        if(!acquired.success)return fail(make(acquired.unsupported?Status::Skip:Status::Fail,acquired.code,acquired.message,acquired.unsupported?ExitCategory::Unsupported:ExitCategory::Driver));
        CommandResult validated=validate_acquisition_data(acquired.value,request);if(validated.status!=Status::Pass)return fail(validated);
        if(trigger&&o.channels.size()>1) { double edge=0;if(!first_edge_seconds(acquired.value,o,edge))return fail(make(Status::Fail,"TRIGGER_REFERENCE_EDGE_MISSING","Reference edge was not detected",ExitCategory::ValidationFailed));edge_times.push_back(edge);
            if(o.trigger_action=="delay_to_start"&&o.target_phase_provided) {double actual,period;if(!measured_start_phase(acquired.value,o,actual,period))return fail(make(Status::Fail,"TRIGGER_REFERENCE_PERIOD_MISSING","At least two reference edges are required",ExitCategory::ValidationFailed));double error_percent=std::fabs(actual-o.target_phase_percent);error_percent=(std::min)(error_percent,100.0-error_percent);double error_us=error_percent*period*1e4;max_delay_error_us=(std::max)(max_delay_error_us,error_us);if(o.delay_tolerance_provided&&error_us>o.max_delay_position_error_us){CommandResult failed=make(Status::Fail,"DELAY_POSITION_MISMATCH","Measured start phase is outside tolerance",ExitCategory::ValidationFailed);failed.evidence["target_phase_percent"]=text(o.target_phase_percent);failed.evidence["actual_phase_percent"]=text(actual);failed.evidence["position_error_us"]=text(error_us);return fail(failed);}}
        }
        std::vector<RawChannel> raw; for(size_t c=0;c<acquired.value.channels.size();++c){RawChannel r;r.name="channel_"+std::to_string(o.channels[c]);r.samples=acquired.value.channels[c].samples;raw.push_back(r);}
        CommandResult written=writer.write_raw(trigger?"trigger":"acquire",repetition,raw); if(written.status!=Status::Pass)return fail(written);
        SummaryRecord s={}; s.test_name=trigger?"trigger":"acquire";s.repetition=repetition;s.requested_sample_rate_hz=o.sample_rate_hz;s.actual_sample_rate_hz=acquired.value.actual_sample_rate_hz;
        s.channel_count=static_cast<unsigned int>(raw.size());s.requested_points_per_channel=o.points_per_channel;s.actual_points_per_channel=raw.empty()?0:raw[0].samples.size();s.expected_duration_seconds=o.points_per_channel/o.sample_rate_hz;s.measured_duration_seconds=acquired.value.duration_seconds;
        s.trigger_enabled=trigger;s.trigger_source=o.trigger_source;s.trigger_edge=o.trigger_edge;s.trigger_action=o.trigger_action;s.trigger_delay=o.trigger_delay_counts;s.trigger_wait_seconds=acquired.value.trigger_wait_seconds;s.wall_elapsed_seconds=acquired.value.wall_elapsed_seconds;s.timed_out=acquired.value.timed_out;s.overrun=acquired.value.overrun;s.cache_overflow=acquired.value.cache_overflow;s.status="PASS";s.code=trigger?"TRIGGER_ACQUISITION_STABLE":"ACQUISITION_STABLE"; summaries.push_back(s);
    }
    if(trigger&&o.jitter_provided&&edge_times.size()>1){double min=*std::min_element(edge_times.begin(),edge_times.end()),max=*std::max_element(edge_times.begin(),edge_times.end());if((max-min)*1e6>o.max_edge_jitter_us)return fail(make(Status::Fail,"TRIGGER_START_JITTER_EXCEEDED","Trigger start jitter exceeded threshold",ExitCategory::ValidationFailed));}
    } catch(const std::exception& error){return fail(make(Status::Fail,"DRIVER_EXCEPTION",error.what(),ExitCategory::Driver));}catch(...){return fail(make(Status::Fail,"DRIVER_EXCEPTION","Unknown driver exception",ExitCategory::Driver));}
    CommandResult ok=make(Status::Pass,trigger?"TRIGGER_ACQUISITION_STABLE":"ACQUISITION_STABLE","Acquisition completed",ExitCategory::Success);ok.evidence["run_directory"]=writer.run_directory();if(trigger&&!edge_times.empty()){double jitter=(*std::max_element(edge_times.begin(),edge_times.end())-*std::min_element(edge_times.begin(),edge_times.end()))*1e6;ok.evidence["measured_start_jitter_us"]=text(jitter);if(o.trigger_action=="delay_to_start"){if(o.target_phase_provided)ok.evidence["max_delay_position_error_us"]=text(max_delay_error_us);else ok.evidence["position_validation"]="not_requested";}}return finish_run(writer,summaries,ok,true);
}

CommandResult capture_phase(DaqAdapter& adapter,const CliOptions& o,const MatrixCase& c)
{
    AdapterResult<CapabilityInfo> metadata=adapter.query_capabilities(o.device);ResultWriter writer(o.output_directory,clock_value); CommandResult made=initialize_run(writer,o,metadata.success?metadata.value.runtime_path:"unavailable",metadata.success?metadata.value.runtime_version:"unavailable");if(made.status!=Status::Pass)return made;std::vector<SummaryRecord> summaries;const auto fail=[&](CommandResult failed){summaries.push_back(summary_for("phase_stitch",failed));failed.evidence["run_directory"]=writer.run_directory();return finish_run(writer,summaries,failed,false);};if(!metadata.success)return fail(make(metadata.unsupported?Status::Skip:Status::Fail,metadata.code,metadata.message,metadata.unsupported?ExitCategory::Unsupported:ExitCategory::Driver));
    std::vector<AcquisitionRequest> requests=phase_capture_requests(c);
    std::vector<Segment> captured_segments;
    try {
    for(size_t i=0;i<requests.size();++i) {
        NoThrowStopGuard stop(adapter);
        AdapterResult<OperationInfo> configured=adapter.configure(requests[i]);if(!configured.success)return fail(make(Status::Fail,configured.code,configured.message,ExitCategory::Driver));
        AdapterResult<AcquisitionData> acquired=adapter.acquire_once(requests[i]);if(!acquired.success)return fail(make(Status::Fail,acquired.code,acquired.message,ExitCategory::Driver));
        CommandResult validated=validate_acquisition_data(acquired.value,requests[i]);if(validated.status!=Status::Pass)return fail(validated);
        if(acquired.value.channels.size()!=2)return fail(make(Status::Fail,"CHANNEL_COUNT_MISMATCH","Phase capture requires two channels",ExitCategory::ValidationFailed));
        std::vector<RawChannel> raw(2);raw[0].name="sample";raw[0].samples=acquired.value.channels[0].samples;raw[1].name="reference";raw[1].samples=acquired.value.channels[1].samples;
        CommandResult written=writer.write_raw("phase_stitch",static_cast<unsigned int>(i),raw);if(written.status!=Status::Pass)return fail(written);
        SummaryRecord summary={};summary.test_name=i==0?"phase_stitch_calibration":"phase_stitch_segment";summary.repetition=static_cast<unsigned int>(i);summary.requested_sample_rate_hz=requests[i].sample_rate_hz;summary.actual_sample_rate_hz=acquired.value.actual_sample_rate_hz;summary.channel_count=2;summary.requested_points_per_channel=requests[i].points_per_channel;summary.actual_points_per_channel=raw[0].samples.size();summary.expected_duration_seconds=requests[i].points_per_channel/requests[i].sample_rate_hz;summary.measured_duration_seconds=acquired.value.duration_seconds;summary.trigger_wait_seconds=acquired.value.trigger_wait_seconds;summary.wall_elapsed_seconds=acquired.value.wall_elapsed_seconds;summary.status="PASS";summary.code="CAPTURED";summaries.push_back(summary);
        Segment segment;segment.id=static_cast<int>(i)+1;segment.acquisition_order=static_cast<int>(i);segment.sample_rate_hz=acquired.value.actual_sample_rate_hz;segment.reference_calibration=i==0;segment.sample_channel=raw[0].samples;segment.reference_channel=raw[1].samples;for(size_t sample=0;sample<segment.sample_channel.size();++sample)segment.timestamps.push_back(sample/segment.sample_rate_hz);captured_segments.push_back(segment);
    }
    }catch(const std::exception& error){return fail(make(Status::Fail,"DRIVER_EXCEPTION",error.what(),ExitCategory::Driver));}catch(...){return fail(make(Status::Fail,"DRIVER_EXCEPTION","Unknown driver exception",ExitCategory::Driver));}
    CommandResult snapshot=writer.write_config_snapshot(o.config_path,"matrix.tsv");if(snapshot.status!=Status::Pass)return fail(snapshot);
    CommandResult reconstructed=reconstruct_phase_segments(captured_segments,c);reconstructed.evidence["run_directory"]=writer.run_directory();summaries.push_back(summary_for("phase_stitch_reconstruct",reconstructed));return finish_run(writer,summaries,reconstructed,reconstructed.status==Status::Pass);
}

MatrixParseResult read_matrix(const std::string& path)
{
    std::ifstream input(path.c_str(),std::ios::binary);
    if(!input) { MatrixParseResult r; r.error.code="CONFIG_OPEN_FAILED"; r.error.message="Cannot open matrix file"; return r; }
    return parse_matrix(std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()));
}

const MatrixCase* first_mode(const Matrix& matrix,const std::string& mode)
{ for(size_t i=0;i<matrix.cases.size();++i) if(matrix.cases[i].enabled && matrix.cases[i].mode.has_value() && matrix.cases[i].mode.value()==mode) return &matrix.cases[i]; return 0; }
}

CliParseResult parse_cli(int argc,char** argv)
{
    CliParseResult parsed; parsed.result=make(Status::Pass,"CLI_OK","Arguments parsed",ExitCategory::Success);
    if(argc>0){parsed.options.executable_variant=argv[0];for(int i=0;i<argc;++i){if(i)parsed.options.invocation+=' ';parsed.options.invocation+=argv[i];}}
    if(argc<2) { parsed.result=make(Status::Fail,"MISSING_COMMAND","Command is required",ExitCategory::InvalidArguments); return parsed; }
    int first_option=2; const std::string command=argv[1];
    if(command=="--help" || command=="-h") parsed.options.command=CliCommand::Help;
    else if(command=="capability") parsed.options.command=CliCommand::Capability;
    else if(command=="acquire") parsed.options.command=CliCommand::Acquire;
    else if(command=="trigger") parsed.options.command=CliCommand::Trigger;
    else if(command=="suite") parsed.options.command=CliCommand::Suite;
    else if(command=="phase-stitch") {
        if(argc<3) { parsed.result=make(Status::Fail,"MISSING_SUBCOMMAND","phase-stitch requires capture or reconstruct",ExitCategory::InvalidArguments); return parsed; }
        const std::string sub=argv[2]; first_option=3;
        if(sub=="capture") parsed.options.command=CliCommand::PhaseCapture;
        else if(sub=="reconstruct") parsed.options.command=CliCommand::PhaseReconstruct;
        else { parsed.result=make(Status::Fail,"UNKNOWN_SUBCOMMAND","Unknown phase-stitch subcommand",ExitCategory::InvalidArguments); return parsed; }
    } else { parsed.result=make(Status::Fail,"UNKNOWN_COMMAND","Unknown command",ExitCategory::InvalidArguments); return parsed; }

    bool have_scope=false;
    for(int i=first_option;i<argc;++i) {
        const std::string arg=argv[i]; std::string value;
        if(arg=="--config") { if(!option_value(argc,argv,i,parsed.options.config_path,parsed.result)) return parsed; }
        else if(arg=="--device") { if(!option_value(argc,argv,i,parsed.options.device,parsed.result)) return parsed; }
        else if(arg=="--input-dir") { if(!option_value(argc,argv,i,parsed.options.input_directory,parsed.result)) return parsed; }
        else if(arg=="--output-dir") { if(!option_value(argc,argv,i,parsed.options.output_directory,parsed.result)) return parsed; }
        else if(arg=="--channels" || arg=="--range" || arg=="--rate" || arg=="--points" || arg=="--repeat" || arg=="--timeout" ||
                arg=="--source" || arg=="--edge" || arg=="--action" || arg=="--delay" || arg=="--jitter" || arg=="--delay-tolerance" || arg=="--target-phase-percent") {
            if(!option_value(argc,argv,i,value,parsed.result)) return parsed; bool valid=true;
            if(arg=="--channels") valid=channels(value,parsed.options.channels); else if(arg=="--range") parsed.options.value_range=value;
            else if(arg=="--rate") valid=numeric(value,parsed.options.sample_rate_hz)&&parsed.options.sample_rate_hz>0;
            else if(arg=="--points") valid=numeric(value,parsed.options.points_per_channel)&&parsed.options.points_per_channel>0;
            else if(arg=="--repeat") valid=numeric(value,parsed.options.repeat_count)&&parsed.options.repeat_count>0;
            else if(arg=="--timeout") valid=numeric(value,parsed.options.timeout_seconds)&&parsed.options.timeout_seconds>0;
            else if(arg=="--source") parsed.options.trigger_source=value; else if(arg=="--edge") {parsed.options.trigger_edge=value;valid=value=="rising"||value=="falling";}
            else if(arg=="--action") {parsed.options.trigger_action=value;valid=value=="none"||value=="delay_to_start";}
            else if(arg=="--delay") valid=numeric(value,parsed.options.trigger_delay_counts)&&parsed.options.trigger_delay_counts>=0;
            else if(arg=="--jitter"){valid=numeric(value,parsed.options.max_edge_jitter_us)&&parsed.options.max_edge_jitter_us>=0;parsed.options.jitter_provided=valid;}
            else if(arg=="--target-phase-percent"){valid=numeric(value,parsed.options.target_phase_percent)&&parsed.options.target_phase_percent>=0&&parsed.options.target_phase_percent<100;parsed.options.target_phase_provided=valid;}
            else {valid=numeric(value,parsed.options.max_delay_position_error_us)&&parsed.options.max_delay_position_error_us>=0;parsed.options.delay_tolerance_provided=valid;}
            if(!valid){parsed.result=make(Status::Fail,"INVALID_OPTION_VALUE","Option value is invalid",ExitCategory::InvalidArguments);parsed.result.evidence["option"]=arg;return parsed;}
        }
        else if(arg=="--all" || arg=="--case" || arg=="--from") {
            if(have_scope) { parsed.result=make(Status::Fail,"CONFLICTING_SCOPE","Only one suite scope may be selected",ExitCategory::InvalidArguments); return parsed; }
            have_scope=true;
            if(arg=="--all") parsed.options.scope=SuiteScope::all();
            else {
                if(!option_value(argc,argv,i,value,parsed.result)) return parsed;
                if(!known_suite_case(value)) { parsed.result=make(Status::Fail,"UNKNOWN_CASE","Unknown suite case",ExitCategory::InvalidArguments); parsed.result.evidence["case"]=value; return parsed; }
                parsed.options.scope=arg=="--case"?SuiteScope::single(value):SuiteScope::from(value);
            }
        } else { parsed.result=make(Status::Fail,"UNKNOWN_OPTION","Unknown command option",ExitCategory::InvalidArguments); parsed.result.evidence["option"]=arg; return parsed; }
    }
    if(parsed.options.device.empty()) parsed.options.device="PCI-1714,BID#0";
    if(parsed.options.command==CliCommand::Suite && parsed.options.config_path.empty()) parsed.result=make(Status::Fail,"MISSING_CONFIG","suite requires --config",ExitCategory::InvalidArguments);
    if(parsed.options.command==CliCommand::PhaseCapture && parsed.options.config_path.empty()) parsed.result=make(Status::Fail,"MISSING_CONFIG","Command requires --config",ExitCategory::InvalidArguments);
    if(parsed.options.command==CliCommand::PhaseCapture && parsed.options.output_directory.empty()) parsed.result=make(Status::Fail,"MISSING_OUTPUT_DIR","capture requires --output-dir",ExitCategory::InvalidArguments);
    if(parsed.options.command==CliCommand::Acquire || parsed.options.command==CliCommand::Trigger) {
        if(parsed.options.channels.empty()||parsed.options.value_range.empty()||parsed.options.sample_rate_hz<=0||!parsed.options.points_per_channel||!parsed.options.repeat_count||parsed.options.timeout_seconds<=0||parsed.options.output_directory.empty())
            parsed.result=make(Status::Fail,"MISSING_ACQUISITION_OPTION","Acquisition parameters and --output-dir are required",ExitCategory::InvalidArguments);
        else if(parsed.options.command==CliCommand::Trigger && (parsed.options.trigger_source.empty()||parsed.options.trigger_edge.empty()||parsed.options.trigger_action.empty()))
            parsed.result=make(Status::Fail,"MISSING_TRIGGER_OPTION","Trigger source, edge and action are required",ExitCategory::InvalidArguments);
    }
    if(parsed.options.command==CliCommand::PhaseReconstruct && parsed.options.input_directory.empty()) parsed.result=make(Status::Fail,"MISSING_INPUT_DIR","reconstruct requires --input-dir",ExitCategory::InvalidArguments);
    return parsed;
}

int run_cli_impl(DaqAdapter& adapter,int argc,char** argv)
{
    CliParseResult parsed=parse_cli(argc,argv); CommandResult final=parsed.result;SuiteResult suite_result;bool is_suite=false;
    if(parsed.ok() && parsed.options.command==CliCommand::Help) {
        std::cout
            << u8"DAQ \u80fd\u529b\u9a8c\u8bc1\u5de5\u5177\n"
            << u8"\u547d\u4ee4\uff1a\n"
            << u8"  capability [--device <\u8bbe\u5907\u63cf\u8ff0>] [--output-dir <\u76ee\u5f55>]\n"
            << u8"  acquire --channels <\u901a\u9053\u5217\u8868> --range <\u91cf\u7a0b> --rate <\u91c7\u6837\u7387> --points <\u70b9\u6570> --repeat <\u6b21\u6570> --timeout <\u79d2> --output-dir <\u76ee\u5f55>\n"
            << u8"  trigger --channels <\u901a\u9053\u5217\u8868> --range <\u91cf\u7a0b> --rate <\u91c7\u6837\u7387> --points <\u70b9\u6570> --repeat <\u6b21\u6570> --timeout <\u79d2> --output-dir <\u76ee\u5f55> --source <\u89e6\u53d1\u6e90> --edge rising|falling --action none|delay_to_start [--delay <\u8ba1\u6570>] [--target-phase-percent <0..100>] [--delay-tolerance <\u5fae\u79d2>] [--jitter <\u5fae\u79d2>]\n"
            << u8"  phase-stitch capture --config <\u914d\u7f6e\u6587\u4ef6> --output-dir <\u76ee\u5f55> [--device <\u8bbe\u5907\u63cf\u8ff0>]\n"
            << u8"  phase-stitch reconstruct --input-dir <\u91c7\u96c6\u76ee\u5f55> --config <\u914d\u7f6e\u6587\u4ef6> [--output-dir <\u76ee\u5f55>]\n"
            << u8"  suite --config <\u914d\u7f6e\u6587\u4ef6> [--output-dir <\u76ee\u5f55>] (--all | --case <\u9a8c\u8bc1\u9879> | --from <\u9a8c\u8bc1\u9879>)\n"
            << u8"\u9009\u9879\uff1a\n"
            << u8"  --help, -h  \u663e\u793a\u6b64\u5e2e\u52a9\u5e76\u9000\u51fa\u3002\n";
        return 0;
    }
    active_cli_options=&parsed.options;active_runtime_path="unavailable";active_runtime_version="unavailable";
    DaqAdapter* execution_adapter=&adapter;std::unique_ptr<CachedCapabilityAdapter> cached_adapter;
    if(parsed.ok()&&parsed.options.command!=CliCommand::PhaseReconstruct){AdapterResult<CapabilityInfo> metadata=safe_query(adapter,parsed.options.device);if(metadata.success){active_runtime_path=metadata.value.runtime_path.empty()?"unavailable":metadata.value.runtime_path;active_runtime_version=metadata.value.runtime_version.empty()?"unavailable":metadata.value.runtime_version;}cached_adapter.reset(new CachedCapabilityAdapter(adapter,parsed.options.device,metadata));execution_adapter=cached_adapter.get();}
    if(parsed.ok()) {
        if(parsed.options.command==CliCommand::Capability) {
            AdapterResult<CapabilityInfo> q=execution_adapter->query_capabilities(parsed.options.device);
            if(q.success){active_runtime_path=q.value.runtime_path.empty()?"unavailable":q.value.runtime_path;active_runtime_version=q.value.runtime_version.empty()?"unavailable":q.value.runtime_version;}
            final=q.success?make(Status::Pass,"DEVICE_CAPABILITY_CONFIRMED","Device capabilities queried",ExitCategory::Success):
                make(q.unsupported?Status::Skip:Status::Fail,q.code,q.message,q.unsupported?ExitCategory::Unsupported:ExitCategory::Driver);
            if(!q.success){const std::string root=parsed.options.output_directory.empty()?"daq_capability_results":parsed.options.output_directory;ResultWriter writer(root,clock_value);CommandResult made=initialize_run(writer,parsed.options);if(made.status!=Status::Pass)final=made;else{final.evidence["run_directory"]=writer.run_directory();std::vector<SummaryRecord> summaries(1,summary_for("capability",final));final=finish_run(writer,summaries,final,false);}}
            if(q.success){const CapabilityInfo& c=q.value;final.evidence["device_description"]=parsed.options.device;final.evidence["runtime_path"]=c.runtime_path.empty()?"unavailable":c.runtime_path;final.evidence["runtime_version"]=c.runtime_version.empty()?"unavailable":c.runtime_version;final.evidence["function_table_version"]=text(c.function_table_version);final.evidence["function_table_revision"]=text(c.function_table_revision);final.evidence["channel_count"]=text(c.max_channels);final.evidence["supported_ranges"]=joined(c.supported_ranges);final.evidence["min_sample_rate_hz"]=text(c.min_sample_rate_hz);final.evidence["max_sample_rate_hz"]=text(c.max_sample_rate_hz);final.evidence["max_points_per_channel"]=text(c.max_points_per_channel);final.evidence["max_scan_count"]=text(c.max_scan_count);final.evidence["buffer_capacity"]=text(c.buffer_capacity);final.evidence["trigger_supported"]=c.supports_trigger?"true":"false";final.evidence["trigger_count"]=text(c.trigger_count);final.evidence["trigger_sources"]=joined(c.trigger_sources);final.evidence["trigger_actions"]=joined(c.trigger_actions);final.evidence["trigger_delay_min"]=text(c.trigger_delay_min);final.evidence["trigger_delay_max"]=text(c.trigger_delay_max);
                const std::string root=parsed.options.output_directory.empty()?"daq_capability_results":parsed.options.output_directory;ResultWriter writer(root,clock_value);CommandResult made=initialize_run(writer,"capability");if(made.status!=Status::Pass)final=made;else{CapabilityRecord record={};record.device_description=parsed.options.device;record.channel_count=c.max_channels;record.min_sample_rate_hz=c.min_sample_rate_hz;record.max_sample_rate_hz=c.max_sample_rate_hz;record.max_scan_count=c.max_scan_count;record.buffer_capacity=c.buffer_capacity;record.trigger_supported=c.supports_trigger;record.trigger_count=c.trigger_count;record.trigger_sources=joined(c.trigger_sources);record.trigger_actions=joined(c.trigger_actions);record.trigger_delay_min=c.trigger_delay_min;record.trigger_delay_max=c.trigger_delay_max;CommandResult written=writer.write_capability(record);if(written.status!=Status::Pass)final=written;else{final.evidence["run_directory"]=writer.run_directory();std::vector<SummaryRecord> summaries(1,summary_for("capability",final));final=finish_run(writer,summaries,final,true);}}}
        } else if(parsed.options.command==CliCommand::Acquire || parsed.options.command==CliCommand::Trigger) {
            final=direct_acquire(*execution_adapter,parsed.options,parsed.options.command==CliCommand::Trigger);
        } else if(parsed.options.command==CliCommand::PhaseReconstruct) {
            if(parsed.options.config_path.empty()) final=make(Status::Fail,"RECONSTRUCT_CONFIG_REQUIRED","reconstruct requires --config thresholds",ExitCategory::InvalidArguments);
            else {
                MatrixParseResult matrix=read_matrix(parsed.options.config_path); final=preflight(matrix);
                const MatrixCase* selected=final.status==Status::Pass?first_mode(matrix.matrix,"phase_stitch"):0;
                if(final.status==Status::Pass) final=selected?reconstruct_phase_directory(parsed.options.input_directory,*selected):make(Status::Fail,"CASE_NOT_CONFIGURED","No enabled phase_stitch row",ExitCategory::InvalidArguments);
                const std::string root=parsed.options.output_directory.empty()?"daq_capability_results":parsed.options.output_directory;ResultWriter writer(root,clock_value);CommandResult made=initialize_run(writer,"phase-stitch reconstruct");if(made.status!=Status::Pass)final=made;else{CommandResult snapshot=writer.write_config_snapshot(parsed.options.config_path,"matrix.tsv");if(snapshot.status!=Status::Pass)final=snapshot;std::vector<SummaryRecord> summaries(1,summary_for("phase_stitch_reconstruct",final));final.evidence["run_directory"]=writer.run_directory();final=finish_run(writer,summaries,final,final.status==Status::Pass);}
            }
        } else {
            MatrixParseResult matrix=read_matrix(parsed.options.config_path); final=preflight(matrix);
            if(final.status==Status::Pass) {
                if(parsed.options.command==CliCommand::Suite) {is_suite=true;const std::string root=parsed.options.output_directory.empty()?"daq_capability_results":parsed.options.output_directory;ResultWriter writer(root,clock_value);CommandResult made=initialize_run(writer,"suite");if(made.status!=Status::Pass){final=made;suite_result.command=final;}else{std::vector<SummaryRecord> acquisition_summaries;suite_result=run_suite(*execution_adapter,matrix.matrix,parsed.options.scope,&writer,&acquisition_summaries);final=suite_result.command;CommandResult snapshot=writer.write_config_snapshot(parsed.options.config_path,"matrix.tsv");if(snapshot.status!=Status::Pass)final=snapshot;std::vector<SummaryRecord> summaries=acquisition_summaries;for(std::map<std::string,SuiteCaseResult>::const_iterator it=suite_result.cases.begin();it!=suite_result.cases.end();++it)if(!it->second.result.code.empty())summaries.push_back(summary_for(it->first,it->second.result));final.evidence["run_directory"]=writer.run_directory();final=finish_run(writer,summaries,final,final.status==Status::Pass);suite_result.command=final;}}
                else {
                    std::string mode=parsed.options.command==CliCommand::Acquire?"acquire":parsed.options.command==CliCommand::Trigger?"trigger":"phase_stitch";
                    const MatrixCase* selected=first_mode(matrix.matrix,mode);
                    if(!selected) final=make(Status::Fail,"CASE_NOT_CONFIGURED","No enabled matrix row matches command",ExitCategory::InvalidArguments);
                    else final=parsed.options.command==CliCommand::PhaseCapture?capture_phase(*execution_adapter,parsed.options,*selected):run_matrix_case(*execution_adapter,matrix.matrix,*selected);
                }
            }
        }
    }
    std::cerr << final.code << ": " << final.message << '\n';
    std::cout << (is_suite?suite_result_json(suite_result):to_json(final)) << '\n';
    return exit_code(final);
}

int run_cli(DaqAdapter& adapter,int argc,char** argv)
{
    try{return run_cli_impl(adapter,argc,argv);}catch(const std::exception& error){CommandResult failed=make(Status::Fail,"DRIVER_EXCEPTION",error.what(),ExitCategory::Driver);std::cerr<<failed.code<<": "<<failed.message<<'\n';std::cout<<to_json(failed)<<'\n';return exit_code(failed);}catch(...){CommandResult failed=make(Status::Fail,"DRIVER_EXCEPTION","Unknown driver exception",ExitCategory::Driver);std::cerr<<failed.code<<": "<<failed.message<<'\n';std::cout<<to_json(failed)<<'\n';return exit_code(failed);}
}
}  // namespace daq_capability_test
