#include "daq_capability_test/legacy_adapter.hpp"
#include "daq_capability_test/trigger_mapping.hpp"
#include "daq_capability_test/adapter_safety.hpp"
#include "../daq_headers/legacy/bdaqctrl.h"

#include <windows.h>
#include <tlhelp32.h>
#include <winver.h>
#ifdef max
#undef max
#endif

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace daq_capability_test {
namespace {
using namespace Automation::BDaq;

template <typename T>
AdapterResult<T> failure(const char* code, const char* message, const char* stage, ErrorCode error = Success)
{
    AdapterResult<T> result;
    result.code = code; result.message = message; result.stage = stage;
    if (error != Success) {
        std::ostringstream out; out << "0x" << std::hex << std::uppercase << static_cast<unsigned int>(error);
        result.driver_error = out.str();
    }
    return result;
}

std::wstring widen(const std::string& value)
{
    if (value.empty()) return std::wstring();
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), 0, 0);
    if (count <= 0) throw std::runtime_error("invalid UTF-8");
    std::wstring output(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &output[0], count))
        throw std::runtime_error("UTF-8 conversion failed");
    return output;
}

std::string narrow(const std::wstring& value)
{
    if (value.empty()) return std::string();
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), 0, 0, 0, 0);
    if (count <= 0) throw std::runtime_error("invalid UTF-16");
    std::string output(static_cast<size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &output[0], count, 0, 0))
        throw std::runtime_error("UTF-16 conversion failed");
    return output;
}

template <typename E>
std::string enum_name(const wchar_t* type, E value)
{
    wchar_t text[128] = {};
    return BioFailed(AdxEnumToString(type, static_cast<int32>(value), 128, text))
        ? std::to_string(static_cast<int>(value)) : narrow(text);
}

template <typename E>
bool parse_enum(const wchar_t* type, const std::string& text, E& value)
{
    int32 parsed = 0;
    const std::wstring wide = widen(text);
    const ErrorCode code = AdxStringToEnum(type, wide.c_str(), &parsed);
    if (BioFailed(code)) return false;
    value = static_cast<E>(parsed); return true;
}

std::string file_version(const std::wstring& path)
{
    DWORD ignored = 0; const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return std::string();
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return std::string();
    VS_FIXEDFILEINFO* info = 0; UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) return std::string();
    std::ostringstream out;
    out << HIWORD(info->dwFileVersionMS) << '.' << LOWORD(info->dwFileVersionMS) << '.'
        << HIWORD(info->dwFileVersionLS) << '.' << LOWORD(info->dwFileVersionLS);
    return out.str();
}

std::string simulator_version()
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
    if(snapshot==INVALID_HANDLE_VALUE) return "unavailable";
    MODULEENTRY32W entry={}; entry.dwSize=sizeof(entry); std::string result="unavailable";
    if(Module32FirstW(snapshot,&entry)) do {
        std::wstring name(entry.szModule); std::transform(name.begin(),name.end(),name.begin(),::towlower);
        if(name.find(L"biosim")!=std::wstring::npos){ result=narrow(entry.szExePath)+" version="+file_version(entry.szExePath); break; }
    } while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot); return result;
}

template <typename T>
class CollectionOwner {
public:
    explicit CollectionOwner(ICollection<T>* value) : value_(value) {}
    ~CollectionOwner() { if (value_) value_->Dispose(); }
    ICollection<T>* get() const { return value_; }
private:
    ICollection<T>* value_;
};
}

class LegacyDaqAdapter::Impl {
public:
    Impl() : controller(0), prepared(false), configured(false) {}
    ~Impl() { dispose(); }

    AdapterResult<OperationInfo> preflight()
    {
        AdapterResult<OperationInfo> ok;
        // This reference is only for preflight inspection. The legacy header owns its
        // separate process-lifetime static module reference used by inline API calls.
        HMODULE module = LoadLibraryW(L"biodaq.dll");
        if (!module) return failure<OperationInfo>("RUNTIME_NOT_FOUND", "biodaq.dll could not be loaded", "runtime_load");
        struct ModuleOwner { HMODULE value; ~ModuleOwner(){ if(value) FreeLibrary(value); } } module_owner{module};
        wchar_t path_buffer[32768] = {};
        const DWORD length = GetModuleFileNameW(module, path_buffer, 32768);
        if (!length) return failure<OperationInfo>("RUNTIME_PATH_UNAVAILABLE", "loaded biodaq.dll path could not be resolved", "runtime_path");
        try { runtime_path = narrow(std::wstring(path_buffer, length)); }
        catch (const std::runtime_error&) { return failure<OperationInfo>("PATH_CONVERSION_FAILED", "runtime path UTF conversion failed", "runtime_path"); }
        runtime_version = file_version(std::wstring(path_buffer, length));
        if (runtime_version.empty()) return failure<OperationInfo>("RUNTIME_VERSION_UNAVAILABLE", "loaded biodaq.dll version could not be read", "runtime_version");
        const std::vector<std::string> exports = legacy_required_runtime_exports();
        for (const std::string& name : exports) {
            if (!GetProcAddress(module, name.c_str())) {
                return classify_legacy_missing_export(name);
            }
        }
        ok.success = true; return ok;
    }

    AdapterResult<OperationInfo> select(const std::string& device)
    {
        AdapterResult<OperationInfo> ready = preflight(); if (!ready.success) return ready;
        if (!controller) controller = AdxBufferedAiCtrlCreate();
        if (!controller) return failure<OperationInfo>("CONTROLLER_CREATE_FAILED", "controller creation returned null", "create");
        std::wstring name; try { name=widen(device); } catch(const std::runtime_error&) { return failure<OperationInfo>("DEVICE_NAME_CONVERSION_FAILED","device name is not valid UTF-8","device_name_conversion"); }
        DeviceInformation information(name.c_str());
        const ErrorCode code = controller->setSelectedDevice(information);
        if (BioFailed(code)) return failure<OperationInfo>("DEVICE_NOT_FOUND", "device selection failed", "select_device", code);
        selected_device = device; return successful();
    }

    AdapterResult<OperationInfo> successful() const { AdapterResult<OperationInfo> r; r.success = true; return r; }
    void stop() noexcept { if (!controller) return; if (prepared) controller->Stop(); controller->Cleanup(); prepared=false; configured=false; }
    void dispose() noexcept { if (controller) { stop(); controller->Dispose(); controller=0; } }

    BufferedAiCtrl* controller; bool prepared; bool configured;
    AcquisitionRequest configured_request;
    std::string selected_device, runtime_path, runtime_version;
    CapabilityInfo capabilities;
};

LegacyDaqAdapter::LegacyDaqAdapter() : impl_(new Impl) {}
LegacyDaqAdapter::~LegacyDaqAdapter() = default;

AdapterResult<CapabilityInfo> LegacyDaqAdapter::query_capabilities(const std::string& device)
{
    impl_->stop();
    AdapterResult<OperationInfo> selected = impl_->select(device);
    if (!selected.success) { AdapterResult<CapabilityInfo> r; r.code=selected.code; r.message=selected.message; r.stage=selected.stage; r.driver_error=selected.driver_error; return r; }
    AiFeatures* features = impl_->controller->getFeatures();
    if (!features) return failure<CapabilityInfo>("FEATURE_QUERY_FAILED", "feature object is null", "get_features");
    CapabilityInfo info; info.supports_acquisition=features->getBufferedAiSupported();
    info.max_channels=static_cast<unsigned int>(std::max<int32>(0, features->getChannelCountMax()));
    const MathInterval rate=features->getConvertClockRange(); info.min_sample_rate_hz=rate.Min; info.max_sample_rate_hz=rate.Max;
    const unsigned int scan_limit=static_cast<unsigned int>(std::max<int32>(0, features->getScanCountMax()));
    info.max_scan_count=scan_limit; info.max_points_per_channel=scan_limit;
    info.buffer_capacity=static_cast<unsigned int>(std::max<int32>(0, impl_->controller->getBufferCapacity()));
    CollectionOwner<ValueRange> ranges_owner(features->getValueRanges()); ICollection<ValueRange>* ranges=ranges_owner.get();
    if (ranges) for (int32 i=0;i<ranges->getCount();++i) info.supported_ranges.push_back(enum_name(L"ValueRange",ranges->getItem(i)));
    info.supports_trigger=features->getTriggerSupported(); info.trigger_count=static_cast<unsigned int>(std::max<int32>(0,features->getTriggerCount()));
    CollectionOwner<SignalDrop> sources_owner(features->getTriggerSources()); ICollection<SignalDrop>* sources=sources_owner.get();
    if (sources) for(int32 i=0;i<sources->getCount();++i) info.trigger_sources.push_back(enum_name(L"SignalDrop",sources->getItem(i)));
    CollectionOwner<TriggerAction> actions_owner(features->getTriggerActions()); ICollection<TriggerAction>* actions=actions_owner.get();
    if (actions) for(int32 i=0;i<actions->getCount();++i) info.trigger_actions.push_back(enum_name(L"TriggerAction",actions->getItem(i)));
    const MathInterval delay=features->getTriggerDelayRange(); info.trigger_delay_min=delay.Min; info.trigger_delay_max=delay.Max;
    info.runtime_path=impl_->runtime_path; info.runtime_version=impl_->runtime_version;
    info.evidence.push_back("buffer_capacity: controller value after device selection");
    impl_->capabilities=info; AdapterResult<CapabilityInfo> result; result.success=true; result.value=info; return result;
}

AdapterResult<OperationInfo> LegacyDaqAdapter::configure(const AcquisitionRequest& request)
{
    impl_->stop(); AdapterResult<OperationInfo> selected=impl_->select(request.device); if(!selected.success)return selected;
    if(request.channels.empty()) return failure<OperationInfo>("INVALID_CHANNELS","at least one channel is required","validate_channels");
    for(size_t i=1;i<request.channels.size();++i) if(request.channels[i]!=request.channels[0]+static_cast<int>(i))
        return failure<OperationInfo>("NON_CONTIGUOUS_CHANNELS","legacy scan channels must be contiguous","validate_channels");
    if(request.points_per_channel>static_cast<unsigned int>(std::numeric_limits<int32>::max()))
        return failure<OperationInfo>("POINT_COUNT_TOO_LARGE","point count exceeds legacy API","validate_points");
    AiFeatures* features=impl_->controller->getFeatures();
    if(!features) return failure<OperationInfo>("FEATURE_QUERY_FAILED","feature object is null","get_features");
    CollectionOwner<ValueRange> ranges_owner(features->getValueRanges()); ICollection<ValueRange>* ranges=ranges_owner.get();
    std::vector<std::string> supported_ranges;
    if(ranges) for(int32 i=0;i<ranges->getCount();++i) supported_ranges.push_back(enum_name(L"ValueRange",ranges->getItem(i)));
    size_t selected_range=0;
    if(!ranges||!select_legacy_supported_voltage_range(request.value_range,supported_ranges,selected_range))
        return failure<OperationInfo>("VALUE_RANGE_UNSUPPORTED","value range does not exactly match a device-supported voltage range","map_range");
    const ValueRange range=ranges->getItem(static_cast<int32>(selected_range));
    AiChannelCollection* channels=impl_->controller->getChannels();
    if(!channels) return failure<OperationInfo>("CHANNEL_QUERY_FAILED","channel collection is null","get_channels");
    for(int channel:request.channels){ if(channel<0||channel>=channels->getCount()) return failure<OperationInfo>("CHANNEL_OUT_OF_RANGE","channel is unavailable","set_range"); ErrorCode c=channels->getItem(channel).setValueRange(range); if(BioFailed(c))return failure<OperationInfo>("CONFIGURE_FAILED","setting channel range failed","set_range",c); }
    ScanChannel* scan=impl_->controller->getScanChannel(); ConvertClock* clock=impl_->controller->getConvertClock();
    if(!scan||!clock)return failure<OperationInfo>("CONFIGURE_FAILED","scan or clock interface unavailable","get_configuration");
#define LEGACY_SET(call, stage_name) do { ErrorCode c=(call); if(BioFailed(c)) return failure<OperationInfo>("CONFIGURE_FAILED", "legacy configuration call failed", stage_name, c); } while(0)
    LEGACY_SET(scan->setChannelStart(request.channels[0]),"set_channel_start"); LEGACY_SET(scan->setChannelCount(static_cast<int32>(request.channels.size())),"set_channel_count");
    LEGACY_SET(scan->setSamples(static_cast<int32>(request.points_per_channel)),"set_samples"); LEGACY_SET(clock->setRate(request.sample_rate_hz),"set_rate");
    ErrorCode prepared=impl_->controller->Prepare(); if(BioFailed(prepared))return failure<OperationInfo>("PREPARE_FAILED","Prepare failed","prepare",prepared);
#undef LEGACY_SET
    impl_->prepared=true; impl_->configured=true; AdapterResult<OperationInfo> result; result.success=true;
    OperationInfo requested; requested.channel_start=request.channels[0]; requested.channel_count=static_cast<unsigned int>(request.channels.size());
    requested.points_per_channel=request.points_per_channel; requested.sample_rate_hz=request.sample_rate_hz;
    result.value.actual_channel_start=scan->getChannelStart(); result.value.actual_channel_count=static_cast<unsigned int>(scan->getChannelCount());
    result.value.actual_samples=static_cast<unsigned int>(scan->getSamples()); result.value.sample_rate_hz=clock->getRate();
    for(int channel:request.channels){ const std::string readback=enum_name(L"ValueRange",channels->getItem(channel).getValueRange()); requested.actual_ranges.push_back(request.value_range); result.value.actual_ranges.push_back(readback); }
    result.value.channel_start=result.value.actual_channel_start; result.value.channel_count=result.value.actual_channel_count;
    result.value.points_per_channel=result.value.actual_samples; result.value.value_range=result.value.actual_ranges.empty()?std::string():result.value.actual_ranges[0];
    AdapterResult<OperationInfo> validated=validate_legacy_config_readback(requested,result.value);
    if(!validated.success){impl_->stop();return validated;}
    impl_->configured_request=request;
    return validated;
}

AdapterResult<OperationInfo> LegacyDaqAdapter::configure_trigger(const TriggerRequest& request)
{
    if(!impl_->controller||!impl_->configured)return failure<OperationInfo>("NOT_CONFIGURED","configure acquisition before trigger","trigger_state");
    AiFeatures* features=impl_->controller->getFeatures(); if(!features||!features->getTriggerSupported()){ AdapterResult<OperationInfo> r=failure<OperationInfo>("TRIGGER_UNSUPPORTED","device does not support trigger","trigger_capability");r.unsupported=true;return r; }
    std::string source_name, edge_name, action_name;
    if(!map_trigger_source(request.source,source_name)||!map_trigger_edge(request.edge,edge_name)||!map_trigger_action(request.action,action_name))
        return failure<OperationInfo>("INVALID_TRIGGER_VALUE","trigger source, edge, or action is not a supported public value","trigger_mapping");
    SignalDrop source; ActiveSignal edge; TriggerAction action;
    if(!parse_enum(L"SignalDrop",source_name,source)||!parse_enum(L"ActiveSignal",edge_name,edge)||!parse_enum(L"TriggerAction",action_name,action)) { AdapterResult<OperationInfo> r=failure<OperationInfo>("TRIGGER_UNSUPPORTED","mapped trigger is unavailable in the runtime","trigger_mapping");r.unsupported=true;return r; }
    Trigger* trigger=impl_->controller->getTrigger(); if(!trigger){ AdapterResult<OperationInfo> r=failure<OperationInfo>("TRIGGER_UNSUPPORTED","trigger interface unavailable","get_trigger");r.unsupported=true;return r; }
    CollectionOwner<SignalDrop> sources_owner(features->getTriggerSources()); CollectionOwner<TriggerAction> actions_owner(features->getTriggerActions());
    ICollection<SignalDrop>* sources=sources_owner.get(); ICollection<TriggerAction>* actions=actions_owner.get();
    bool source_supported=false,action_supported=false;
    if(sources) for(int32 i=0;i<sources->getCount();++i) source_supported=source_supported||sources->getItem(i)==source;
    if(actions) for(int32 i=0;i<actions->getCount();++i) action_supported=action_supported||actions->getItem(i)==action;
    const MathInterval delay=features->getTriggerDelayRange();
    if(!source_supported||!action_supported||request.delay_counts<delay.Min||request.delay_counts>delay.Max){ AdapterResult<OperationInfo> r=failure<OperationInfo>("TRIGGER_UNSUPPORTED","requested trigger is outside device capabilities","trigger_capability");r.unsupported=true;return r; }
#define TRIGGER_SET(call, stage_name) do { ErrorCode c=(call); if(BioFailed(c)) return failure<OperationInfo>("TRIGGER_CONFIG_FAILED", "trigger configuration failed", stage_name, c); } while(0)
    TRIGGER_SET(trigger->setSource(source),"set_trigger_source"); TRIGGER_SET(trigger->setEdge(edge),"set_trigger_edge"); TRIGGER_SET(trigger->setAction(action),"set_trigger_action"); TRIGGER_SET(trigger->setDelayCount(request.delay_counts),"set_trigger_delay");
#undef TRIGGER_SET
    if(trigger->getSource()!=source||trigger->getEdge()!=edge||trigger->getAction()!=action||trigger->getDelayCount()!=request.delay_counts)return failure<OperationInfo>("TRIGGER_READBACK_MISMATCH","trigger readback differs","trigger_readback");
    AdapterResult<OperationInfo> result; result.success=true; return result;
}

AdapterResult<AcquisitionData> LegacyDaqAdapter::acquire_once(const AcquisitionRequest& request)
{
    if(!impl_->configured){ AdapterResult<OperationInfo> configured=configure(request); if(!configured.success){ AdapterResult<AcquisitionData> r; r.code=configured.code;r.message=configured.message;r.stage=configured.stage;r.driver_error=configured.driver_error;return r; }}
    else if(!same_acquisition_request(impl_->configured_request,request)) return failure<AcquisitionData>("INVALID_ACQUISITION_REQUEST","request differs from the successful configuration","validate_request");
    const size_t channels=request.channels.size(); if(channels&&request.points_per_channel>std::numeric_limits<size_t>::max()/channels)return failure<AcquisitionData>("POINT_COUNT_OVERFLOW","total point count overflow","allocate");
    const size_t total=channels*request.points_per_channel; if(total>static_cast<size_t>(std::numeric_limits<int32>::max()))return failure<AcquisitionData>("POINT_COUNT_TOO_LARGE","GetData count exceeds legacy API","allocate");
    AcquisitionData data; std::vector<double> chunk;
    const size_t chunk_capacity=(std::min)(total,static_cast<size_t>(1024*1024));
    try { data.channels.resize(channels); for(auto& channel:data.channels) channel.samples.resize(request.points_per_channel); chunk.resize(chunk_capacity); }
    catch(const std::bad_alloc&){return failure<AcquisitionData>("ALLOCATION_FAILED","sample buffer allocation failed","allocate");}
    catch(const std::length_error&){return failure<AcquisitionData>("ALLOCATION_FAILED","sample buffer size is invalid","allocate");}
    const auto start=std::chrono::steady_clock::now(); ErrorCode code=impl_->controller->Start(); if(BioFailed(code))return failure<AcquisitionData>("RUN_FAILED","Start failed","start",code);
    const double timeout=request.timeout_seconds>0.0?request.timeout_seconds:(request.points_per_channel/request.sample_rate_hz+5.0);
    const auto deadline=start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(timeout));
    while(impl_->controller->getState()!=Stopped){
        if(std::chrono::steady_clock::now()>=deadline){
            ErrorCode stopped=impl_->controller->Stop(); impl_->controller->Cleanup(); impl_->prepared=false;impl_->configured=false;
            AdapterResult<AcquisitionData> r;
            if(BioFailed(stopped)) r=failure<AcquisitionData>("STOP_FAILED","Stop failed after acquisition timeout","timeout_stop",stopped);
            else r=failure<AcquisitionData>("TIMEOUT","acquisition deadline expired","wait_stopped");
            r.value.timed_out=true;r.value.timeout_observable=true;return r; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for(size_t offset=0;offset<total;){
        const size_t count=(std::min)(chunk_capacity,total-offset); code=impl_->controller->GetData(static_cast<int32>(count),chunk.data());
        if(BioFailed(code))return failure<AcquisitionData>("READ_FAILED","GetData failed","get_data",code);
        for(size_t index=0;index<count;++index){ const size_t global=offset+index; data.channels[global%channels].samples[global/channels]=chunk[index]; }
        offset+=count;
    }
    data.actual_sample_rate_hz=impl_->controller->getConvertClock()->getRate();map_acquisition_timing(data,request.points_per_channel,data.actual_sample_rate_hz,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    int32 overrun=0,overflow=0; ErrorCode oe=impl_->controller->GetEventStatus(EvtBufferedAiOverrun,overrun), ce=impl_->controller->GetEventStatus(EvtBufferedAiCacheOverflow,overflow);
    if(BioFailed(oe)||BioFailed(ce))return failure<AcquisitionData>("EVENT_STATUS_FAILED","event status query failed","event_status",BioFailed(oe)?oe:ce);
    data.overrun=overrun!=0;data.cache_overflow=overflow!=0;data.timed_out=false;data.timeout_observable=true;
    data.evidence.push_back("finite Start monitored until Stopped with deadline enforcement");
    data.layout="legacy GetData is treated as scan-major interleaved; verified by pure deinterleave test, hardware confirmation required";
    data.layout_unverified=true;
    data.layout_requires_hardware_confirmation=true;
    AdapterResult<AcquisitionData> result;result.success=true;result.value=data;return result;
}

void LegacyDaqAdapter::stop() noexcept { impl_->stop(); }

AdapterResult<AcquisitionData> LegacyDaqAdapter::verify_demo_interleaving(const std::string& device)
{
    impl_->dispose();
    AdapterResult<OperationInfo> selected=impl_->select(device);
    if(!selected.success){ AdapterResult<AcquisitionData> r; r.code=selected.code;r.message=selected.message;r.stage=selected.stage;return r; }
    AiChannelCollection* channels=impl_->controller->getChannels();
    if(!channels||channels->getCount()<2){ impl_->dispose(); return failure<AcquisitionData>("LAYOUT_VALIDATION_UNAVAILABLE","two Demo channels are required","layout_setup"); }
    const ValueRange original0=channels->getItem(0).getValueRange(), original1=channels->getItem(1).getValueRange();
    const std::string original0_name=enum_name(L"ValueRange",original0), original1_name=enum_name(L"ValueRange",original1);
    AdapterResult<AcquisitionData> outcome;
    ErrorCode code=channels->getItem(0).setValueRange(V_Neg10To10);
    if(!BioFailed(code)) code=channels->getItem(1).setValueRange(V_0To1);
    ScanChannel* scan=impl_->controller->getScanChannel(); ConvertClock* clock=impl_->controller->getConvertClock();
    if(!BioFailed(code)&&scan&&clock) code=scan->setChannelStart(0);
    if(!BioFailed(code)) code=scan->setChannelCount(2);
    if(!BioFailed(code)) code=scan->setSamples(8);
    if(!BioFailed(code)) code=clock->setRate(1000.0);
    if(!BioFailed(code)) code=impl_->controller->Prepare();
    if(!BioFailed(code)){ impl_->prepared=true; code=impl_->controller->RunOnce(); }
    std::vector<double> raw(16);
    if(!BioFailed(code)) code=impl_->controller->GetData(16,raw.data());
    const std::string simulator=simulator_version();
    if(BioFailed(code)) outcome=failure<AcquisitionData>("LAYOUT_VALIDATION_ACQUIRE_FAILED","Demo layout acquisition failed","layout_acquire",code);
    else if(!legacy_demo_layout_matches_ranges(raw,0.001)){
        outcome=failure<AcquisitionData>("LAYOUT_MISMATCH","same-acquisition range oracle rejected scan-major layout","layout_oracle");
        std::ostringstream values; for(size_t i=0;i<raw.size();++i) values<<(i?",":"")<<raw[i]; outcome.evidence.push_back("raw="+values.str());
    } else {
        outcome.success=true; outcome.code="LAYOUT_VERIFIED_BY_DEMO"; outcome.value.layout="scan_major_interleaved";
        outcome.value.layout_unverified=false; outcome.value.layout_requires_hardware_confirmation=true;
        outcome.evidence.push_back("odd_values_within_V_0To1=true"); outcome.evidence.push_back("even_value_above_1V=true");
        outcome.evidence.push_back("real_PCI_layout_requires_hardware_confirmation=true");
    }
    impl_->dispose();

    AdapterResult<OperationInfo> restore_selected=impl_->select(device);
    bool restored=restore_selected.success;
    std::string restored0_name,restored1_name;
    if(restored){
        AiChannelCollection* restore_channels=impl_->controller->getChannels(); restored=restore_channels&&restore_channels->getCount()>=2;
        if(restored) restored=!BioFailed(restore_channels->getItem(0).setValueRange(original0));
        if(restored) restored=!BioFailed(restore_channels->getItem(1).setValueRange(original1));
        if(restored){restored0_name=enum_name(L"ValueRange",restore_channels->getItem(0).getValueRange());restored1_name=enum_name(L"ValueRange",restore_channels->getItem(1).getValueRange());restored=restored0_name==original0_name&&restored1_name==original1_name;}
    }
    impl_->dispose();
    outcome.evidence.push_back("original_ranges="+original0_name+","+original1_name);
    outcome.evidence.push_back("restored_ranges="+restored0_name+","+restored1_name);
    outcome.evidence.push_back("BioSimulator="+simulator);
    if(!restored){ AdapterResult<AcquisitionData> failed=failure<AcquisitionData>("LAYOUT_RESTORE_FAILED","Demo channel ranges could not be restored","layout_restore");failed.evidence=outcome.evidence;return failed; }
    return outcome;
}
}  // namespace daq_capability_test
