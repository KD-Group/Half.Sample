#pragma once

#include <string>

namespace daq_capability_test {

inline bool map_trigger_edge(const std::string& value, std::string& mapped)
{
    if (value == "rising" || value == "RisingEdge") mapped = "RisingEdge";
    else if (value == "falling" || value == "FallingEdge") mapped = "FallingEdge";
    else return false;
    return true;
}

inline bool map_trigger_action(const std::string& value, std::string& mapped)
{
    if (value == "none" || value == "ActionNone") mapped = "ActionNone";
    else if (value == "delay_to_start" || value == "DelayToStart") mapped = "DelayToStart";
    else if (value == "delay_to_stop" || value == "DelayToStop") mapped = "DelayToStop";
    else return false;
    return true;
}

inline bool map_trigger_source(const std::string& value, std::string& mapped)
{
    if (value == "external" || value == "external_analog" || value == "SigExtAnaTrigger") mapped = "SigExtAnaTrigger";
    else if (value == "external_digital_0" || value == "SigExtDigTrigger0") mapped = "SigExtDigTrigger0";
    else if (value == "external_digital_1" || value == "SigExtDigTrigger1") mapped = "SigExtDigTrigger1";
    else return false;
    return true;
}

}  // namespace daq_capability_test
