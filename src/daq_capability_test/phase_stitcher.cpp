#include "daq_capability_test/phase_stitcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <set>

namespace daq_capability_test {
namespace {

struct PreparedSegment {
    Segment segment;
    std::vector<double> phases;
    std::string phase_source;
    std::string period_source;
    int edge_count = 0;
    double measured_period = 0.0;
    double frequency_error_percent = 0.0;
    double measured_duty_percent = 0.0;
    double max_jitter_us = 0.0;
    std::vector<int> bin_counts;
};

struct Builder {
    std::vector<std::vector<double> > values;
    std::vector<std::vector<int> > orders;
    std::vector<int> segment_ids;
    std::string phase_source;
    std::string period_source;
    int edge_count = 0;
    double measured_period = 0.0, frequency_error = 0.0, duty = 0.0, jitter = 0.0;
};

bool finite(double value) { return std::isfinite(value) != 0; }

std::string number(double value)
{
    std::ostringstream out;
    out.precision(12);
    out << value;
    return out.str();
}

std::string integers(const std::vector<int>& values)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        out << values[i];
    }
    return out.str();
}

CommandResult result(Status status, ExitCategory category, const std::string& code)
{
    CommandResult r;
    r.status = status;
    r.exit_category = category;
    r.code = code;
    return r;
}

StitchResult failure(const std::string& code, ExitCategory category, int requested, int reconstructed,
                     const std::string& empty_bins = "", int valid_segments = 0)
{
    StitchResult r;
    r.command = result(Status::Fail, category, code);
    r.command.evidence["requested_waveforms"] = number(requested);
    r.command.evidence["reconstructed_waveforms"] = number(reconstructed);
    r.command.evidence["empty_bins"] = empty_bins;
    r.command.evidence["valid_segments"] = number(valid_segments);
    return r;
}

bool valid_config(const StitchConfig& c)
{
    return finite(c.signal_frequency_hz) && c.signal_frequency_hz > 0.0 && c.phase_bin_count > 0 &&
        c.min_samples_per_bin > 0 && c.requested_waveforms > 0 && c.max_attempts > 0 &&
        finite(c.reference_threshold_v) && finite(c.reference_hysteresis_v) && c.reference_hysteresis_v >= 0.0 &&
        finite(c.min_reference_span_v) && c.min_reference_span_v >= 0.0 &&
        finite(c.max_frequency_error_percent) && c.max_frequency_error_percent >= 0.0 &&
        finite(c.max_edge_jitter_us) && c.max_edge_jitter_us >= 0.0 &&
        finite(c.reference_duty_cycle_percent) && c.reference_duty_cycle_percent > 0.0 && c.reference_duty_cycle_percent < 100.0 &&
        finite(c.max_duty_cycle_error_percent) && c.max_duty_cycle_error_percent >= 0.0 &&
        finite(c.min_overlap_percent) && c.min_overlap_percent > 0.0 && c.min_overlap_percent <= 100.0 &&
        finite(c.max_overlap_error_v) && c.max_overlap_error_v >= 0.0 &&
        finite(c.max_response_drift_v) && c.max_response_drift_v >= 0.0 &&
        finite(c.max_boundary_jump_v) && c.max_boundary_jump_v >= 0.0;
}

bool valid_segment(const Segment& s)
{
    if (s.sample_channel.empty() || !finite(s.sample_rate_hz) || s.sample_rate_hz <= 0.0 ||
        !finite(s.trigger_delay_seconds) || (s.has_known_phase && (!finite(s.known_phase) || s.known_phase < 0.0 || s.known_phase >= 1.0))) return false;
    for (std::size_t i = 0; i < s.sample_channel.size(); ++i) if (!finite(s.sample_channel[i])) return false;
    if (!s.reference_channel.empty() && s.reference_channel.size() != s.sample_channel.size()) return false;
    for (std::size_t i = 0; i < s.reference_channel.size(); ++i) if (!finite(s.reference_channel[i])) return false;
    if (!s.timestamps.empty()) {
        if (s.timestamps.size() != s.sample_channel.size()) return false;
        for (std::size_t i = 0; i < s.timestamps.size(); ++i) {
            if (!finite(s.timestamps[i]) || (i && s.timestamps[i] <= s.timestamps[i - 1])) return false;
            if (i) {
                const double expected=1.0/s.sample_rate_hz;
                if (std::fabs((s.timestamps[i]-s.timestamps[i-1])-expected)/expected>0.01) return false;
            }
        }
    }
    return true;
}

double time_at(const Segment& s, std::size_t i)
{
    return s.timestamps.empty() ? static_cast<double>(i) / s.sample_rate_hz : s.timestamps[i] - s.timestamps[0];
}

struct EdgeEvents { std::vector<double> rising, falling; };

EdgeEvents detect_edges(const Segment& s, const StitchConfig& c)
{
    EdgeEvents events;
    const double low = c.reference_threshold_v - c.reference_hysteresis_v * 0.5;
    const double high = c.reference_threshold_v + c.reference_hysteresis_v * 0.5;
    bool rising_armed=s.reference_channel[0]<=low, falling_armed=s.reference_channel[0]>=high;
    bool rising_pending=false,falling_pending=false; double rising_time=0,falling_time=0;
    const double minimum_interval = 0.5 / c.signal_frequency_hz;
    for (std::size_t i = 1; i < s.reference_channel.size(); ++i) {
        const double previous=s.reference_channel[i-1], value=s.reference_channel[i];
        if(value<=low && !rising_pending) rising_armed=true;
        if(value>=high && !falling_pending) falling_armed=true;
        const double t0=time_at(s,i-1),t1=time_at(s,i);
        if(rising_armed && previous<c.reference_threshold_v && value>=c.reference_threshold_v) {
            rising_time=t0+(c.reference_threshold_v-previous)/(value-previous)*(t1-t0); rising_pending=true;
        }
        if(falling_armed && previous>c.reference_threshold_v && value<=c.reference_threshold_v) {
            falling_time=t0+(previous-c.reference_threshold_v)/(previous-value)*(t1-t0); falling_pending=true;
        }
        if(rising_pending && value>=high){if(events.rising.empty() || rising_time-events.rising.back()>=minimum_interval)events.rising.push_back(rising_time);rising_pending=false;rising_armed=false;}
        else if(rising_pending && value<=low)rising_pending=false;
        if(falling_pending && value<=low){if(events.falling.empty() || falling_time-events.falling.back()>=minimum_interval)events.falling.push_back(falling_time);falling_pending=false;falling_armed=false;}
        else if(falling_pending && value>=high)falling_pending=false;
    }
    return events;
}

std::string calibrate(const Segment& s, const StitchConfig& c, PreparedSegment& out)
{
    if(s.reference_channel.empty()) return "REFERENCE_CALIBRATION_REQUIRED";
    const EdgeEvents detected=detect_edges(s,c);
    const std::vector<double>& rising=detected.rising; const std::vector<double>& falling=detected.falling;
    out.edge_count=static_cast<int>(rising.size()+falling.size());
    if(rising.size()<2) return "REFERENCE_CALIBRATION_REQUIRED";
    std::vector<double> periods;
    for(std::size_t i=1;i<rising.size();++i) periods.push_back(rising[i]-rising[i-1]);
    double sum=0; for(std::size_t i=0;i<periods.size();++i) sum+=periods[i];
    out.measured_period=sum/periods.size();
    out.frequency_error_percent=std::fabs(1.0/out.measured_period-c.signal_frequency_hz)/c.signal_frequency_hz*100.0;
    if(out.frequency_error_percent>c.max_frequency_error_percent) return "REFERENCE_FREQUENCY_MISMATCH";
    for(std::size_t i=0;i<periods.size();++i) out.max_jitter_us=std::max(out.max_jitter_us,std::fabs(periods[i]-out.measured_period)*1e6);
    if(out.max_jitter_us>c.max_edge_jitter_us) return "EDGE_JITTER_EXCEEDED";
    double duty_sum=0; int count=0;
    for(std::size_t i=0;i+1<rising.size();++i) for(std::size_t j=0;j<falling.size();++j)
        if(falling[j]>rising[i] && falling[j]<rising[i+1]) { duty_sum+=(falling[j]-rising[i])/(rising[i+1]-rising[i])*100.0; ++count; break; }
    if(!count) return "REFERENCE_CALIBRATION_REQUIRED";
    out.measured_duty_percent=duty_sum/count;
    if(std::fabs(out.measured_duty_percent-c.reference_duty_cycle_percent)>c.max_duty_cycle_error_percent)
        return "REFERENCE_DUTY_CYCLE_MISMATCH";
    return "";
}

std::string prepare(const Segment& s, const StitchConfig& c, PreparedSegment& out)
{
    out.segment = s;
    double origin = 0.0;
    double period = 1.0 / c.signal_frequency_hz;
    if (s.has_known_phase) {
        origin = s.known_phase + s.trigger_delay_seconds * c.signal_frequency_hz;
        out.phase_source = "metadata";
        out.period_source = "configured";
    } else {
        if (s.reference_channel.empty()) return "SEGMENT_PHASE_UNKNOWN";
        const std::pair<std::vector<double>::const_iterator, std::vector<double>::const_iterator> range =
            std::minmax_element(s.reference_channel.begin(), s.reference_channel.end());
        if (*range.second - *range.first < c.min_reference_span_v) return "REFERENCE_SPAN_TOO_LOW";
        const EdgeEvents detected=detect_edges(s,c);
        EdgeDirection actual_direction=c.edge_direction;
        std::vector<double> found = c.edge_direction==EdgeDirection::Rising ? detected.rising : detected.falling;
        if(found.empty()) {
            actual_direction=c.edge_direction==EdgeDirection::Rising ? EdgeDirection::Falling : EdgeDirection::Rising;
            found=actual_direction==EdgeDirection::Rising ? detected.rising : detected.falling;
        }
        if (found.empty()) return "EDGE_NOT_FOUND";
        out.edge_count=static_cast<int>(found.size());
        const double edge_phase=actual_direction==EdgeDirection::Rising ? 0.0 : c.reference_duty_cycle_percent/100.0;
        origin = edge_phase-found.front() / period;
        out.phase_source = "reference_edge";
        out.period_source = "estimated";
        if (found.size() >= 2) {
            std::vector<double> periods;
            for (std::size_t i = 1; i < found.size(); ++i) periods.push_back(found[i] - found[i - 1]);
            double sum = 0.0;
            for (std::size_t i = 0; i < periods.size(); ++i) sum += periods[i];
            period = sum / periods.size();
            const double frequency_error = std::fabs(1.0 / period - c.signal_frequency_hz) / c.signal_frequency_hz * 100.0;
            out.measured_period=period; out.frequency_error_percent=frequency_error;
            if (frequency_error > c.max_frequency_error_percent) return "REFERENCE_FREQUENCY_MISMATCH";
            double max_deviation = 0.0;
            for (std::size_t i = 0; i < periods.size(); ++i) max_deviation = std::max(max_deviation, std::fabs(periods[i] - period));
            out.max_jitter_us=max_deviation*1e6;
            if (out.max_jitter_us > c.max_edge_jitter_us) return "EDGE_JITTER_EXCEEDED";
            origin = edge_phase-found.front() / period;
            out.period_source = "measured";
        }
    }
    for (std::size_t i = 0; i < s.sample_channel.size(); ++i) {
        double phase = origin + time_at(s, i) / period;
        phase -= std::floor(phase);
        out.phases.push_back(phase);
    }
    return "";
}

std::vector<int> missing(const Builder& b, const StitchConfig& c)
{
    std::vector<int> bins;
    for (int i = 0; i < c.phase_bin_count; ++i)
        if (static_cast<int>(b.values[i].size()) < c.min_samples_per_bin) bins.push_back(i);
    return bins;
}

void add(Builder& b, const PreparedSegment& s, const StitchConfig& c)
{
    b.segment_ids.push_back(s.segment.id);
    if (b.phase_source.empty()) b.phase_source = s.phase_source;
    if (b.period_source.empty()) b.period_source = s.period_source;
    b.edge_count+=s.edge_count; b.measured_period=std::max(b.measured_period,s.measured_period);
    b.frequency_error=std::max(b.frequency_error,s.frequency_error_percent);
    b.duty=std::max(b.duty,s.measured_duty_percent); b.jitter=std::max(b.jitter,s.max_jitter_us);
    for (std::size_t i = 0; i < s.phases.size(); ++i) {
        int bin = static_cast<int>(std::floor(s.phases[i] * c.phase_bin_count + 1e-9));
        if (bin >= c.phase_bin_count) bin = c.phase_bin_count - 1;
        b.values[bin].push_back(s.segment.sample_channel[i]);
        b.orders[bin].push_back(s.segment.acquisition_order);
    }
}

double overlap_percent(const Builder& b)
{
    int overlap = 0;
    for (std::size_t i = 0; i < b.values.size(); ++i) if (b.values[i].size() > 1) ++overlap;
    return 100.0 * overlap / b.values.size();
}

std::string validate_builder(const Builder& b, const StitchConfig& c, double& overlap_error, double& drift)
{
    overlap_error = 0.0;
    drift = 0.0;
    if (b.segment_ids.size() < 2) return "";
    for (std::size_t bin = 0; bin < b.values.size(); ++bin) {
        const std::vector<double>& values = b.values[bin];
        if (values.size() > 1) {
            const std::pair<std::vector<double>::const_iterator, std::vector<double>::const_iterator> range =
                std::minmax_element(values.begin(), values.end());
            overlap_error = std::max(overlap_error, *range.second - *range.first);
            drift=std::max(drift,*range.second-*range.first);
            double mean_order=0,mean_value=0; int min_order=b.orders[bin][0],max_order=b.orders[bin][0];
            for(std::size_t i=0;i<values.size();++i){mean_order+=b.orders[bin][i];mean_value+=values[i];min_order=std::min(min_order,b.orders[bin][i]);max_order=std::max(max_order,b.orders[bin][i]);}
            mean_order/=values.size(); mean_value/=values.size(); double covariance=0,variance=0;
            for(std::size_t i=0;i<values.size();++i){covariance+=(b.orders[bin][i]-mean_order)*(values[i]-mean_value);variance+=(b.orders[bin][i]-mean_order)*(b.orders[bin][i]-mean_order);}
            if(variance>0) drift=std::max(drift,std::fabs(covariance/variance*(max_order-min_order)));
        }
    }
    if (overlap_error > c.max_overlap_error_v) return "OVERLAP_MISMATCH";
    if (drift > c.max_response_drift_v) return "NON_STATIONARY_RESPONSE";
    return "";
}
}  // namespace

StitchResult stitch_phase_waveforms(const std::vector<Segment>& segments, const StitchConfig& c)
{
    if (!valid_config(c)) return failure("INVALID_STITCH_CONFIG", ExitCategory::InvalidArguments, c.requested_waveforms, 0);
    std::vector<PreparedSegment> prepared;
    std::vector<Segment> ordinary_segments;
    std::set<int> ids, orders;
    std::map<std::string,std::string> invalid_evidence;
    PreparedSegment calibration_data; std::vector<PreparedSegment> calibrations; int calibration_id=0, invalid_count=0;
    std::string calibration_error, normal_error;
    const std::size_t attempted_limit=std::min(segments.size(),static_cast<std::size_t>(c.max_attempts));
    for (std::size_t i = 0; i < attempted_limit; ++i) {
        if(segments[i].id<=0 || !ids.insert(segments[i].id).second)
            return failure("INVALID_SEGMENT_ID",ExitCategory::InvalidArguments,c.requested_waveforms,0);
        if(segments[i].acquisition_order<0 || !orders.insert(segments[i].acquisition_order).second)
            return failure("INVALID_SEGMENT",ExitCategory::InvalidArguments,c.requested_waveforms,0);
        if (!valid_segment(segments[i])) return failure("INVALID_SEGMENT", ExitCategory::InvalidArguments, c.requested_waveforms, 0);
        if(!segments[i].reference_calibration) { ordinary_segments.push_back(segments[i]); continue; }
        PreparedSegment p; p.segment=segments[i];
        const std::string error = calibrate(segments[i],c,p);
        if (!error.empty()) {
            const std::string prefix="invalid_"+number(invalid_count)+"_"; ++invalid_count;
            invalid_evidence[prefix+"id"]=number(segments[i].id); invalid_evidence[prefix+"code"]=error;
            invalid_evidence[prefix+"message"]="Segment rejected during phase preparation";
            invalid_evidence[prefix+"edge_count"]=number(p.edge_count);
            calibration_error=error;
            continue;
        }
        calibrations.push_back(p);
    }
    if(calibrations.empty()) {
        StitchResult failed=failure(calibration_error.empty()?"REFERENCE_CALIBRATION_REQUIRED":calibration_error,
            ExitCategory::ValidationFailed,c.requested_waveforms,0);
        failed.command.evidence.insert(invalid_evidence.begin(),invalid_evidence.end());
        failed.command.evidence["invalid_segment_count"]=number(invalid_count); return failed;
    }
    std::sort(calibrations.begin(),calibrations.end(),[](const PreparedSegment& a,const PreparedSegment& b){
        if(a.segment.acquisition_order!=b.segment.acquisition_order)return a.segment.acquisition_order<b.segment.acquisition_order;
        return a.segment.id<b.segment.id;
    });
    std::vector<int> calibration_ids;
    for(std::size_t i=0;i<calibrations.size();++i) calibration_ids.push_back(calibrations[i].segment.id);
    for(std::size_t base=0;base<calibrations.size();++base) for(std::size_t i=base+1;i<calibrations.size();++i) {
        const double period_error=std::fabs(1.0/calibrations[i].measured_period-1.0/calibrations[base].measured_period)/(1.0/calibrations[base].measured_period)*100.0;
        const double duty_error=std::fabs(calibrations[i].measured_duty_percent-calibrations[base].measured_duty_percent);
        if(period_error>c.max_frequency_error_percent || duty_error>c.max_duty_cycle_error_percent) {
            StitchResult failed=failure("REFERENCE_CALIBRATION_INCONSISTENT",ExitCategory::ValidationFailed,c.requested_waveforms,0);
            failed.command.evidence["calibration_segment_ids"]=integers(calibration_ids);
            for(std::size_t j=0;j<calibrations.size();++j){const std::string p="calibration_"+number(j)+"_";failed.command.evidence[p+"id"]=number(calibrations[j].segment.id);
                failed.command.evidence[p+"period_seconds"]=number(calibrations[j].measured_period);failed.command.evidence[p+"duty_percent"]=number(calibrations[j].measured_duty_percent);}
            failed.command.evidence.insert(invalid_evidence.begin(),invalid_evidence.end());
            failed.command.evidence["invalid_segment_count"]=number(invalid_count);
            return failed;
        }
    }
    calibration_data=calibrations.back(); calibration_id=calibration_data.segment.id;
    StitchConfig measured_config=c;
    measured_config.signal_frequency_hz=1.0/calibration_data.measured_period;
    measured_config.reference_duty_cycle_percent=calibration_data.measured_duty_percent;
    for(std::size_t i=0;i<ordinary_segments.size();++i) {
        PreparedSegment p; const std::string error=prepare(ordinary_segments[i],measured_config,p);
        if(!error.empty()) {
            const std::string prefix="invalid_"+number(invalid_count)+"_"; ++invalid_count;
            invalid_evidence[prefix+"id"]=number(ordinary_segments[i].id); invalid_evidence[prefix+"code"]=error;
            invalid_evidence[prefix+"message"]="Segment rejected during phase preparation";
            invalid_evidence[prefix+"edge_count"]=number(p.edge_count); normal_error=error;
        } else prepared.push_back(p);
    }
    if(prepared.empty() && !normal_error.empty()) {
        StitchResult failed=failure(normal_error,ExitCategory::ValidationFailed,c.requested_waveforms,0);
        failed.command.evidence.insert(invalid_evidence.begin(),invalid_evidence.end());
        failed.command.evidence["invalid_segment_count"]=number(invalid_count); return failed;
    }
    std::sort(prepared.begin(), prepared.end(), [](const PreparedSegment& a, const PreparedSegment& b) {
        if (a.segment.acquisition_order != b.segment.acquisition_order) return a.segment.acquisition_order < b.segment.acquisition_order;
        return a.segment.id < b.segment.id;
    });
    for(std::size_t i=0;i<prepared.size();++i) {
        prepared[i].bin_counts.assign(c.phase_bin_count,0);
        for(std::size_t j=0;j<prepared[i].phases.size();++j){int bin=static_cast<int>(std::floor(prepared[i].phases[j]*c.phase_bin_count+1e-9));if(bin>=c.phase_bin_count)bin=c.phase_bin_count-1;++prepared[i].bin_counts[bin];}
    }
    const auto finalize_failure=[&](StitchResult failed) {
        failed.command.evidence.insert(invalid_evidence.begin(),invalid_evidence.end());
        failed.command.evidence["invalid_segment_count"]=number(invalid_count);
        return failed;
    };

    StitchResult output;
    const std::size_t candidate_limit=prepared.size();
    std::vector<bool> used(candidate_limit,false);
    const int attempts=static_cast<int>(attempted_limit);
    std::vector<int> last_missing;
    while (output.waveforms.size() < static_cast<std::size_t>(c.requested_waveforms)) {
        Builder b;
        b.values.resize(c.phase_bin_count);
        b.orders.resize(c.phase_bin_count);
        last_missing=missing(b,c);
        while (true) {
            int best=-1, best_fill=-1, best_future=-1, best_overlap=-1;
            for(std::size_t i=0;i<candidate_limit;++i) if(!used[i]) {
                int fill=0, overlap=0;
                for(int bin=0;bin<c.phase_bin_count;++bin) {
                    const int deficit=std::max(0,c.min_samples_per_bin-static_cast<int>(b.values[bin].size()));
                    fill+=std::min(deficit,prepared[i].bin_counts[bin]);
                    if(deficit==0) overlap+=prepared[i].bin_counts[bin];
                }
                int future=std::numeric_limits<int>::max();
                if(output.waveforms.size()+1<static_cast<std::size_t>(c.requested_waveforms)) {
                    std::vector<int> available(c.phase_bin_count,0);
                    for(std::size_t k=0;k<candidate_limit;++k) if(!used[k] && k!=i) {
                        for(int bin=0;bin<c.phase_bin_count;++bin) if(prepared[k].bin_counts[bin]) ++available[bin];
                    }
                    future=*std::min_element(available.begin(),available.end());
                }
                if(fill>best_fill || (fill==best_fill && future>best_future) || (fill==best_fill && future==best_future && overlap>best_overlap)) {
                    best=static_cast<int>(i); best_fill=fill; best_future=future; best_overlap=overlap;
                }
            }
            if(best<0 || (best_fill==0 && best_overlap==0)) break;
            used[best]=true;
            add(b,prepared[best],c);
            last_missing = missing(b, c);
            const bool full_single_segment = b.segment_ids.size() == 1 && last_missing.empty();
            if (last_missing.empty() && (full_single_segment || overlap_percent(b) >= c.min_overlap_percent)) break;
        }
        if (!last_missing.empty()) {
            if(attempted_limit>=static_cast<std::size_t>(c.max_attempts)) {
                StitchResult failed=failure("MAX_ATTEMPTS_EXCEEDED",ExitCategory::ValidationFailed,c.requested_waveforms,
                    static_cast<int>(output.waveforms.size()),integers(last_missing),static_cast<int>(candidate_limit));
                failed.command.evidence.insert(invalid_evidence.begin(),invalid_evidence.end());
                failed.command.evidence["invalid_segment_count"]=number(invalid_count); failed.command.evidence["attempts_used"]=number(attempted_limit);
                failed.command.evidence["max_attempts"]=number(c.max_attempts); return finalize_failure(failed);
            }
            const bool empty_bin = std::find_if(b.values.begin(), b.values.end(), [](const std::vector<double>& v) { return v.empty(); }) != b.values.end();
            StitchResult failed=failure(empty_bin && b.phase_source!="reference_edge" ? "EMPTY_PHASE_BINS" : "INSUFFICIENT_PHASE_COVERAGE", ExitCategory::ValidationFailed,
                           c.requested_waveforms, static_cast<int>(output.waveforms.size()), integers(last_missing), static_cast<int>(prepared.size()));
            failed.command.evidence["missing_bin_count"]=number(last_missing.size()); failed.command.evidence["required_samples_per_bin"]=number(c.min_samples_per_bin);
            failed.command.evidence["segment_ids"]=integers(b.segment_ids); return finalize_failure(failed);
        }
        if (b.segment_ids.size() > 1 && overlap_percent(b) < c.min_overlap_percent) {
            StitchResult failed=failure("INSUFFICIENT_SEGMENT_OVERLAP", ExitCategory::ValidationFailed, c.requested_waveforms,
                           static_cast<int>(output.waveforms.size()), "", static_cast<int>(prepared.size()));
            failed.command.evidence["measured_overlap_percent"]=number(overlap_percent(b)); failed.command.evidence["allowed_min_overlap_percent"]=number(c.min_overlap_percent);
            failed.command.evidence["segment_ids"]=integers(b.segment_ids); return finalize_failure(failed);
        }
        double overlap_error = 0.0, drift = 0.0;
        const std::string validation = validate_builder(b, c, overlap_error, drift);
        if (!validation.empty()) {
            StitchResult failed=failure(validation, ExitCategory::ValidationFailed, c.requested_waveforms,
                                                 static_cast<int>(output.waveforms.size()), "", static_cast<int>(prepared.size()));
            failed.command.evidence["measured_overlap_error_v"]=number(overlap_error); failed.command.evidence["allowed_overlap_error_v"]=number(c.max_overlap_error_v);
            failed.command.evidence["measured_response_drift_v"]=number(drift); failed.command.evidence["allowed_response_drift_v"]=number(c.max_response_drift_v);
            failed.command.evidence["segment_ids"]=integers(b.segment_ids); return finalize_failure(failed);
        }
        ReconstructedWaveform waveform;
        waveform.segment_ids = b.segment_ids;
        waveform.coverage_percent = 100.0;
        for (int bin = 0; bin < c.phase_bin_count; ++bin) {
            waveform.phase_bins.push_back((bin + 0.5) / c.phase_bin_count);
            double sum = 0.0;
            for (std::size_t i = 0; i < b.values[bin].size(); ++i) sum += b.values[bin][i];
            waveform.values.push_back(sum / b.values[bin].size());
        }
        const double boundary_jump = std::fabs(waveform.values.front() - waveform.values.back());
        if (boundary_jump > c.max_boundary_jump_v) {
            StitchResult failed=failure("WAVEFORM_BOUNDARY_DISCONTINUITY", ExitCategory::ValidationFailed, c.requested_waveforms,
                           static_cast<int>(output.waveforms.size()), "", static_cast<int>(prepared.size()));
            failed.command.evidence["measured_boundary_jump_v"]=number(boundary_jump); failed.command.evidence["allowed_boundary_jump_v"]=number(c.max_boundary_jump_v);
            failed.command.evidence["segment_ids"]=integers(b.segment_ids); return finalize_failure(failed);
        }
        waveform.evidence["phase_source"] = b.phase_source;
        waveform.evidence["period_source"] = b.period_source;
        waveform.evidence["segment_ids"] = integers(b.segment_ids);
        waveform.evidence["coverage_percent"] = "100";
        waveform.evidence["empty_bins"] = "";
        waveform.evidence["empty_bin_count"] = "0";
        waveform.evidence["min_overlap_percent"] = number(c.min_overlap_percent);
        waveform.evidence["actual_overlap_percent"] = number(overlap_percent(b));
        waveform.evidence["max_overlap_error_v"] = number(overlap_error);
        waveform.evidence["max_drift_v"] = number(drift);
        waveform.evidence["max_response_drift_v"] = number(drift);
        waveform.evidence["boundary_jump_v"] = number(boundary_jump);
        waveform.evidence["edge_count"] = number(b.edge_count);
        waveform.evidence["measured_period_seconds"] = number(b.measured_period);
        waveform.evidence["frequency_error_percent"] = number(b.frequency_error);
        waveform.evidence["measured_duty_cycle_percent"] = number(b.duty);
        waveform.evidence["max_edge_jitter_us"] = number(b.jitter);
        output.waveforms.push_back(waveform);
        bool remaining=false; for(std::size_t i=0;i<candidate_limit;++i) if(!used[i]) remaining=true;
        if(!remaining && output.waveforms.size()<static_cast<std::size_t>(c.requested_waveforms)) break;
    }
    if (output.waveforms.size() != static_cast<std::size_t>(c.requested_waveforms))
        return finalize_failure(failure(attempts>=c.max_attempts ? "MAX_ATTEMPTS_EXCEEDED" : "INSUFFICIENT_COMPLETE_WAVEFORMS",
                       ExitCategory::ValidationFailed, c.requested_waveforms, static_cast<int>(output.waveforms.size()),
                       integers(last_missing), static_cast<int>(prepared.size())));
    output.command = result(Status::Pass, ExitCategory::Success, "N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED");
    output.command.evidence["requested_waveforms"] = number(c.requested_waveforms);
    output.command.evidence["reconstructed_waveforms"] = number(output.waveforms.size());
    for (std::size_t i = 0; i < output.waveforms.size(); ++i)
        output.command.evidence["waveform_" + number(i + 1) + "_segment_ids"] = integers(output.waveforms[i].segment_ids);
    output.command.evidence["coverage_percent"] = "100";
    output.command.evidence["empty_bins"] = "";
    output.command.evidence["min_overlap_percent"] = number(c.min_overlap_percent);
    int edge_count=calibration_data.edge_count; double measured_period=calibration_data.measured_period;
    double frequency_error=calibration_data.frequency_error_percent,duty=calibration_data.measured_duty_percent,jitter=calibration_data.max_jitter_us;
    output.command.evidence["edge_count"]=number(edge_count); output.command.evidence["measured_period_seconds"]=number(measured_period);
    output.command.evidence["frequency_error_percent"]=number(frequency_error); output.command.evidence["measured_duty_cycle_percent"]=number(duty);
    output.command.evidence["max_edge_jitter_us"]=number(jitter); output.command.evidence["empty_bin_count"]="0";
    output.command.evidence["calibration_segment_id"]=number(calibration_id);
    output.command.evidence["calibration_segment_ids"]=integers(calibration_ids);
    output.command.evidence["calibration_measured_period_seconds"]=number(measured_period);
    output.command.evidence["calibration_duty_cycle_percent"]=number(duty);
    output.command.evidence["calibration_max_edge_jitter_us"]=number(jitter);
    output.command.evidence["invalid_segment_count"]=number(invalid_count);
    output.command.evidence.insert(invalid_evidence.begin(),invalid_evidence.end());
    double max_overlap_error = 0.0, max_drift = 0.0, max_boundary_jump = 0.0;
    for (std::size_t i = 0; i < output.waveforms.size(); ++i) {
        max_overlap_error = std::max(max_overlap_error,
            std::atof(output.waveforms[i].evidence["max_overlap_error_v"].c_str()));
        max_drift = std::max(max_drift, std::atof(output.waveforms[i].evidence["max_drift_v"].c_str()));
        max_boundary_jump = std::max(max_boundary_jump,
            std::atof(output.waveforms[i].evidence["boundary_jump_v"].c_str()));
    }
    output.command.evidence["max_overlap_error_v"] = number(max_overlap_error);
    output.command.evidence["max_drift_v"] = number(max_drift);
    output.command.evidence["boundary_jump_v"] = number(max_boundary_jump);
    std::vector<double> baselines, amplitudes, x;
    for(std::size_t w=0;w<output.waveforms.size();++w) {
        double sum=0; for(std::size_t bin=0;bin<output.waveforms[w].values.size();++bin) sum+=output.waveforms[w].values[bin];
        baselines.push_back(sum/output.waveforms[w].values.size());
        const std::pair<std::vector<double>::iterator,std::vector<double>::iterator> range=std::minmax_element(output.waveforms[w].values.begin(),output.waveforms[w].values.end());
        amplitudes.push_back(*range.second-*range.first);
        int order=std::numeric_limits<int>::max();
        for(std::size_t p=0;p<prepared.size();++p) if(std::find(output.waveforms[w].segment_ids.begin(),output.waveforms[w].segment_ids.end(),prepared[p].segment.id)!=output.waveforms[w].segment_ids.end()) order=std::min(order,prepared[p].segment.acquisition_order);
        x.push_back(order);
    }
    double baseline_drift=0, amplitude_drift=0, shape_drift=0, trend_drift=0;
    if(baselines.size()>1) {
        baseline_drift=*std::max_element(baselines.begin(),baselines.end())-*std::min_element(baselines.begin(),baselines.end());
        amplitude_drift=*std::max_element(amplitudes.begin(),amplitudes.end())-*std::min_element(amplitudes.begin(),amplitudes.end());
        for(std::size_t a=0;a<output.waveforms.size();++a) for(std::size_t b=a+1;b<output.waveforms.size();++b)
            for(std::size_t bin=0;bin<output.waveforms[a].values.size();++bin)
                shape_drift=std::max(shape_drift,std::fabs((output.waveforms[a].values[bin]-baselines[a])-(output.waveforms[b].values[bin]-baselines[b])));
        double mx=0,my=0; for(std::size_t i=0;i<x.size();++i){mx+=x[i];my+=baselines[i];} mx/=x.size();my/=x.size();
        double covariance=0,variance=0; for(std::size_t i=0;i<x.size();++i){covariance+=(x[i]-mx)*(baselines[i]-my);variance+=(x[i]-mx)*(x[i]-mx);}
        if(variance>0) trend_drift=std::fabs(covariance/variance*(*std::max_element(x.begin(),x.end())-*std::min_element(x.begin(),x.end())));
    }
    const double response_drift=std::max(std::max(baseline_drift,amplitude_drift),std::max(shape_drift,trend_drift));
    if(response_drift>c.max_response_drift_v) {
        StitchResult failed=failure("NON_STATIONARY_RESPONSE",ExitCategory::ValidationFailed,c.requested_waveforms,0);
        failed.command.evidence["baseline_drift_v"]=number(baseline_drift); failed.command.evidence["amplitude_drift_v"]=number(amplitude_drift);
        failed.command.evidence["shape_drift_v"]=number(shape_drift); failed.command.evidence["trend_drift_v"]=number(trend_drift);
        failed.command.evidence["allowed_response_drift_v"]=number(c.max_response_drift_v); return finalize_failure(failed);
    }
    output.command.evidence["baseline_drift_v"]=number(baseline_drift); output.command.evidence["amplitude_drift_v"]=number(amplitude_drift);
    output.command.evidence["shape_drift_v"]=number(shape_drift); output.command.evidence["trend_drift_v"]=number(trend_drift);
    output.command.evidence["max_response_drift_v"]=number(std::max(max_drift,response_drift));
    return output;
}

}  // namespace daq_capability_test
