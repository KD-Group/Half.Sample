#include "daq_capability_test/matrix.hpp"

#include <cerrno>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <locale>
#include <set>
#include <sstream>

namespace daq_capability_test {
namespace {

const char* columns[] = {"case_name","enabled","device","mode","sample_channel","reference_channel",
    "value_range","sample_rate_hz","points_per_channel","repeat_count","timeout_seconds",
    "signal_frequency_hz","min_complete_cycles","min_signal_span_v","max_edge_jitter_us","target_waveforms",
    "phase_bin_count","min_samples_per_bin","min_overlap_percent","max_overlap_error_v","max_response_drift_v",
    "max_boundary_jump_v","max_attempts","trigger_source","trigger_edge","trigger_action","trigger_delay_counts","trigger_delay_target_phase_percent",
    "max_delay_position_error_us","reference_duty_cycle_percent","max_duty_cycle_error_percent","mock_scenario"};
const unsigned int column_count = sizeof(columns) / sizeof(columns[0]);

std::vector<std::string> split(const std::string& line, char delimiter)
{
    std::vector<std::string> values; std::string value;
    for (std::string::const_iterator it=line.begin(); it!=line.end(); ++it) {
        if (*it==delimiter) { values.push_back(value); value.clear(); } else value += *it;
    }
    values.push_back(value); return values;
}

bool ignored(const std::string& line)
{
    const std::string whitespace=" \t\r"; const std::string::size_type first=line.find_first_not_of(whitespace);
    return first==std::string::npos || line[first]=='#';
}

MatrixParseResult failure(const std::string& code, unsigned int line, const std::string& field,
                          const std::string& case_name, const std::string& message)
{
    MatrixParseResult result; result.error.code=code; result.error.line=line; result.error.field=field;
    result.error.case_name=case_name; result.error.message=message; return result;
}

bool integer(const std::string& text, int& value)
{
    if (text.empty()) return false; size_t i=text[0]=='-' ? 1 : 0; if (i==text.size()) return false;
    for (;i<text.size();++i) if (text[i]<'0' || text[i]>'9') return false;
    std::istringstream input(text); input.imbue(std::locale::classic()); input >> std::noskipws >> value;
    return input && input.peek()==std::char_traits<char>::eof();
}
bool number(const std::string& text, double& value)
{
    if (text.empty()) return false; size_t i=text[0]=='-' ? 1 : 0; if (i==text.size()) return false;
    size_t digits=0; while(i<text.size() && text[i]>='0' && text[i]<='9'){++i;++digits;} if(!digits)return false;
    if(i<text.size() && text[i]=='.'){++i; size_t fraction=0; while(i<text.size()&&text[i]>='0'&&text[i]<='9'){++i;++fraction;} if(!fraction)return false;}
    if(i<text.size() && (text[i]=='e'||text[i]=='E')) { ++i; if(i<text.size()&&(text[i]=='+'||text[i]=='-'))++i;
        size_t exponent=0; while(i<text.size()&&text[i]>='0'&&text[i]<='9'){++i;++exponent;} if(!exponent)return false; }
    if(i!=text.size())return false; std::istringstream input(text); input.imbue(std::locale::classic()); input >> std::noskipws >> value;
    return input && input.peek()==std::char_traits<char>::eof() && std::isfinite(value);
}

template<typename T, typename Parser>
bool assign(MatrixValue<T>& target, const std::string& text, Parser parser)
{
    if (text.empty()) return true;
    if (text=="REQUIRED") { target=MatrixValue<T>::required(); return true; }
    T value; if (!parser(text,value)) return false; target=MatrixValue<T>::from_value(value); return true;
}
bool string_value(const std::string& text, std::string& value) { value=text; return true; }
bool int_list(const std::string& text, std::vector<int>& values)
{
    std::vector<std::string> parts=split(text,','); if (parts.empty()) return false;
    for (size_t i=0;i<parts.size();++i) { int value; if (!integer(parts[i],value)) return false; values.push_back(value); }
    return true;
}
bool double_list(const std::string& text, std::vector<double>& values)
{
    std::vector<std::string> parts=split(text,','); if(parts.empty()) return false;
    for(size_t i=0;i<parts.size();++i){double value;if(!number(parts[i],value))return false;values.push_back(value);}return true;
}

std::string line_text(unsigned int value) { std::ostringstream out; out<<value; return out.str(); }

CommandResult invalid(const std::string& code, const std::string& message, const std::string& field,
                      const std::string& name, unsigned int line)
{
    CommandResult result; result.status=Status::Fail; result.exit_category=ExitCategory::InvalidArguments;
    result.code=code; result.message=message; if (!field.empty()) result.evidence["field"]=field;
    if (!name.empty()) result.evidence["case"]=name; if (line) result.evidence["line"]=line_text(line); return result;
}

bool range_format(const std::string& value)
{
    const std::string::size_type separator=value.find('~');
    if (separator==std::string::npos || value.find('~',separator+1)!=std::string::npos) return false;
    std::string left=value.substr(0,separator), right=value.substr(separator+1);
    if (left.size()<2 || right.size()<2 || left[left.size()-1]!='V' || right[right.size()-1]!='V') return false;
    double low, high; return number(left.substr(0,left.size()-1),low) &&
        number(right.substr(0,right.size()-1),high) && low<high;
}

}  // namespace

MatrixParseResult parse_matrix(const std::string& text)
{
    std::istringstream input(text); std::string line; unsigned int line_number=0; bool have_header=false;
    std::vector<std::string> header; std::set<std::string> names; MatrixParseResult result;
    while (std::getline(input,line)) {
        ++line_number; if (!line.empty() && line[line.size()-1]=='\r') line.erase(line.size()-1);
        if (ignored(line)) continue;
        std::vector<std::string> cells=split(line,'\t');
        if (!have_header) {
            header=cells; have_header=true; std::set<std::string> seen;
            for (size_t i=0;i<header.size();++i) {
                bool known=false; for (unsigned int j=0;j<column_count;++j) if (header[i]==columns[j]) known=true;
                if (!known) return failure("UNKNOWN_COLUMN",line_number,header[i],"","Unknown matrix column");
                if (!seen.insert(header[i]).second) return failure("DUPLICATE_COLUMN",line_number,header[i],"","Duplicate matrix column");
            }
            for (unsigned int i=0;i<column_count-1;++i) if (!seen.count(columns[i]))
                return failure("MISSING_COLUMN",line_number,columns[i],"","Required matrix column is missing");
            continue;
        }
        if (cells.size()!=header.size()) return failure("COLUMN_COUNT_MISMATCH",line_number,"","","TSV row has wrong column count");
        std::map<std::string,std::string> cell; for (size_t i=0;i<header.size();++i) cell[header[i]]=cells[i];
        MatrixCase value; value.line=line_number; value.case_name=cell["case_name"];
        if (value.case_name.empty()) return failure("MISSING_VALUE",line_number,"case_name","","case_name is required");
        if (!names.insert(value.case_name).second) return failure("DUPLICATE_CASE_NAME",line_number,"case_name",value.case_name,"Duplicate case_name");
        if (cell["enabled"]=="true") value.enabled=true; else if (cell["enabled"]=="false") value.enabled=false;
        else return failure("INVALID_BOOLEAN",line_number,"enabled",value.case_name,"enabled must be true or false");
#define STR(name) assign(value.name,cell[#name],string_value)
#define INT(name) assign(value.name,cell[#name],integer)
#define DBL(name) assign(value.name,cell[#name],number)
        if (!STR(device)||!STR(mode)||!STR(value_range)||!STR(trigger_source)||!STR(trigger_edge)||!STR(trigger_action)||!STR(mock_scenario))
            return failure("INVALID_VALUE",line_number,"",value.case_name,"Invalid string value");
        const char* int_names[]={"sample_channel","reference_channel","points_per_channel","repeat_count","min_complete_cycles","phase_bin_count","min_samples_per_bin","max_attempts"};
        bool ints[]={INT(sample_channel),INT(reference_channel),INT(points_per_channel),INT(repeat_count),INT(min_complete_cycles),INT(phase_bin_count),INT(min_samples_per_bin),INT(max_attempts)};
        for (unsigned int i=0;i<8;++i) if (!ints[i]) return failure("INVALID_INTEGER",line_number,int_names[i],value.case_name,"Integer is invalid");
        const char* double_names[]={"sample_rate_hz","timeout_seconds","signal_frequency_hz","min_signal_span_v","max_edge_jitter_us","min_overlap_percent","max_overlap_error_v","max_response_drift_v","max_boundary_jump_v","max_delay_position_error_us","reference_duty_cycle_percent","max_duty_cycle_error_percent"};
        bool doubles[]={DBL(sample_rate_hz),DBL(timeout_seconds),DBL(signal_frequency_hz),DBL(min_signal_span_v),DBL(max_edge_jitter_us),DBL(min_overlap_percent),DBL(max_overlap_error_v),DBL(max_response_drift_v),DBL(max_boundary_jump_v),DBL(max_delay_position_error_us),DBL(reference_duty_cycle_percent),DBL(max_duty_cycle_error_percent)};
        for (unsigned int i=0;i<12;++i) if (!doubles[i]) return failure("INVALID_NUMBER",line_number,double_names[i],value.case_name,"Number is invalid");
        if (!assign(value.target_waveforms,cell["target_waveforms"],int_list)) return failure("INVALID_LIST",line_number,"target_waveforms",value.case_name,"Comma list is invalid");
        if (!assign(value.trigger_delay_counts,cell["trigger_delay_counts"],int_list)) return failure("INVALID_LIST",line_number,"trigger_delay_counts",value.case_name,"Integer comma list is invalid");
        if (!assign(value.trigger_delay_target_phase_percent,cell["trigger_delay_target_phase_percent"],double_list)) return failure("INVALID_LIST",line_number,"trigger_delay_target_phase_percent",value.case_name,"Number comma list is invalid");
#undef STR
#undef INT
#undef DBL
        result.matrix.cases.push_back(value);
    }
    if (!have_header) return failure("MISSING_HEADER",0,"","","Matrix header is missing");
    return result;
}

CommandResult preflight(const MatrixParseResult& parsed)
{
    if (!parsed.ok()) return invalid(parsed.error.code,parsed.error.message,parsed.error.field,parsed.error.case_name,parsed.error.line);
    return preflight(parsed.matrix);
}

CommandResult preflight(const Matrix& matrix)
{
    for (size_t i=0;i<matrix.cases.size();++i) {
        const MatrixCase& c=matrix.cases[i];
#define NEED(name) do { if(c.name.is_required()) return invalid("REQUIRED_THRESHOLD_MISSING","Required matrix value is unresolved",#name,c.case_name,c.line); if(c.name.is_empty()) return invalid("MISSING_REQUIRED_FIELD","Required matrix value is empty",#name,c.case_name,c.line); } while(0)
#define BAD(name, condition) do { if(condition) return invalid("INVALID_FIELD","Invalid enabled-case value",#name,c.case_name,c.line); } while(0)
        NEED(mode);
        const std::string& mode=c.mode.value();
        std::string expected_mode;
        if (c.case_name=="preflight") expected_mode="preflight";
        else if (c.case_name=="device_capability") expected_mode="capability";
        else if (c.case_name=="single_channel_boundary" || c.case_name=="dual_channel_reference" ||
                 c.case_name=="low_sample_rate_500k" || c.case_name=="low_sample_rate_200k" ||
                 c.case_name=="low_sample_rate_100k") expected_mode="acquire";
        else if (c.case_name=="segment_phase") expected_mode="segment_phase";
        else if (c.case_name=="phase_stitch") expected_mode="phase_stitch";
        else if (c.case_name=="external_trigger") expected_mode="trigger";
        else if (c.case_name=="delay_trigger") expected_mode="delay_trigger";
        else return invalid("UNKNOWN_CASE","Unknown matrix case_name","case_name",c.case_name,c.line);
        if (mode!=expected_mode) {
            CommandResult result=invalid("CASE_MODE_MISMATCH","case_name does not match validation mode","mode",c.case_name,c.line);
            result.evidence["expected_mode"]=expected_mode; result.evidence["actual_mode"]=mode; return result;
        }
        if (!c.enabled) continue;
        NEED(device); BAD(device,c.device.value().empty());
        BAD(mode,mode!="preflight" && mode!="capability" && mode!="acquire" && mode!="segment_phase" &&
            mode!="phase_stitch" && mode!="trigger" && mode!="delay_trigger");
        if (mode=="preflight" || mode=="capability") continue;
        NEED(sample_channel); BAD(sample_channel,c.sample_channel.value()<0); NEED(value_range); BAD(value_range,!range_format(c.value_range.value()));
        NEED(sample_rate_hz); BAD(sample_rate_hz,c.sample_rate_hz.value()<=0); NEED(points_per_channel); BAD(points_per_channel,c.points_per_channel.value()<=0);
        NEED(repeat_count); BAD(repeat_count,c.repeat_count.value()<=0); NEED(timeout_seconds); BAD(timeout_seconds,c.timeout_seconds.value()<=0);
        const bool low_rate=c.case_name=="low_sample_rate_500k" || c.case_name=="low_sample_rate_200k" ||
            c.case_name=="low_sample_rate_100k";
        const bool dual=c.case_name=="dual_channel_reference" || mode=="segment_phase" || mode=="phase_stitch";
        if (low_rate || mode=="segment_phase" || mode=="phase_stitch") { NEED(signal_frequency_hz); BAD(signal_frequency_hz,c.signal_frequency_hz.value()<=0); }
        if (low_rate) { NEED(min_complete_cycles); BAD(min_complete_cycles,c.min_complete_cycles.value()<=0); }
        if (low_rate || dual) { NEED(min_signal_span_v); BAD(min_signal_span_v,c.min_signal_span_v.value()<=0); }
        if (dual) { NEED(reference_channel); BAD(reference_channel,c.reference_channel.value()<0 || c.reference_channel.value()==c.sample_channel.value()); }
        if (mode=="segment_phase" || mode=="phase_stitch") {
            NEED(max_edge_jitter_us); BAD(max_edge_jitter_us,c.max_edge_jitter_us.value()<=0);
            NEED(reference_duty_cycle_percent); BAD(reference_duty_cycle_percent,c.reference_duty_cycle_percent.value()<=0 || c.reference_duty_cycle_percent.value()>=100);
            NEED(max_duty_cycle_error_percent); BAD(max_duty_cycle_error_percent,c.max_duty_cycle_error_percent.value()<0);
        }
        if (mode=="phase_stitch") {
            NEED(target_waveforms); BAD(target_waveforms,c.target_waveforms.value().empty());
            for(size_t j=0;j<c.target_waveforms.value().size();++j) BAD(target_waveforms,c.target_waveforms.value()[j]<=0);
            NEED(phase_bin_count); BAD(phase_bin_count,c.phase_bin_count.value()<=0); NEED(min_samples_per_bin); BAD(min_samples_per_bin,c.min_samples_per_bin.value()<=0);
            NEED(max_attempts); BAD(max_attempts,c.max_attempts.value()<2); NEED(min_overlap_percent); BAD(min_overlap_percent,c.min_overlap_percent.value()<=0 || c.min_overlap_percent.value()>100);
            NEED(max_overlap_error_v); BAD(max_overlap_error_v,c.max_overlap_error_v.value()<0); NEED(max_response_drift_v); BAD(max_response_drift_v,c.max_response_drift_v.value()<0);
            NEED(max_boundary_jump_v); BAD(max_boundary_jump_v,c.max_boundary_jump_v.value()<0);
        }
        if (mode=="trigger" || mode=="delay_trigger") {
            NEED(trigger_source); BAD(trigger_source,c.trigger_source.value().empty()); NEED(trigger_edge); BAD(trigger_edge,c.trigger_edge.value()!="rising" && c.trigger_edge.value()!="falling");
            NEED(trigger_action); BAD(trigger_action,c.trigger_action.value()!="none" && c.trigger_action.value()!="delay_to_start"); NEED(max_edge_jitter_us); BAD(max_edge_jitter_us,c.max_edge_jitter_us.value()<=0);
        }
        if (mode=="delay_trigger") {
            NEED(reference_channel); BAD(reference_channel,c.reference_channel.value()<0 || c.reference_channel.value()==c.sample_channel.value());
            NEED(signal_frequency_hz); BAD(signal_frequency_hz,c.signal_frequency_hz.value()<=0);
            NEED(trigger_delay_counts); BAD(trigger_delay_counts,c.trigger_delay_counts.value().size()<2);
            NEED(trigger_delay_target_phase_percent); BAD(trigger_delay_target_phase_percent,c.trigger_delay_target_phase_percent.value().size()!=c.trigger_delay_counts.value().size());
            for(size_t j=0;j<c.trigger_delay_counts.value().size();++j){BAD(trigger_delay_counts,c.trigger_delay_counts.value()[j]<0);BAD(trigger_delay_target_phase_percent,c.trigger_delay_target_phase_percent.value()[j]<0||c.trigger_delay_target_phase_percent.value()[j]>=100);}
            NEED(max_delay_position_error_us); BAD(max_delay_position_error_us,c.max_delay_position_error_us.value()<0);
            NEED(reference_duty_cycle_percent); BAD(reference_duty_cycle_percent,c.reference_duty_cycle_percent.value()<=0||c.reference_duty_cycle_percent.value()>=100);NEED(max_duty_cycle_error_percent);BAD(max_duty_cycle_error_percent,c.max_duty_cycle_error_percent.value()<0);
            const double span=100.0*c.points_per_channel.value()*c.signal_frequency_hz.value()/c.sample_rate_hz.value();BAD(points_per_channel,span>=100.0);
            std::vector<std::pair<double,double> > intervals;for(size_t j=0;j<c.trigger_delay_target_phase_percent.value().size();++j){double a=c.trigger_delay_target_phase_percent.value()[j],b=a+span;if(b<=100)intervals.push_back(std::make_pair(a,b));else{intervals.push_back(std::make_pair(a,100.0));intervals.push_back(std::make_pair(0.0,b-100.0));}}
            std::sort(intervals.begin(),intervals.end());double end=0;for(size_t j=0;j<intervals.size();++j){BAD(trigger_delay_target_phase_percent,intervals[j].first>end+1e-9);end=(std::max)(end,intervals[j].second);}BAD(trigger_delay_target_phase_percent,end<100.0-1e-9);
        }
#undef NEED
#undef BAD
    }
    CommandResult result; result.status=Status::Pass; result.exit_category=ExitCategory::Success;
    result.code="PREFLIGHT_OK"; result.message="Matrix preflight passed"; return result;
}

}  // namespace daq_capability_test
