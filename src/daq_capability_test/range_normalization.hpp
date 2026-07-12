#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace daq_capability_test {

inline bool parse_voltage_number(const std::string& text,double& value)
{if(text.empty())return false;char* end=0;errno=0;value=std::strtod(text.c_str(),&end);return errno==0&&end&&*end=='\0'&&std::isfinite(value);}
inline bool voltage_range_endpoints(const std::string& input,double& low,double& high)
{std::string s;for(size_t i=0;i<input.size();++i)if(input[i]!=' '&&input[i]!='\t')s+=input[i]>='A'&&input[i]<='Z'?static_cast<char>(input[i]-'A'+'a'):input[i];if(s.find("v_neg")==0){size_t split=s.find("to",5);double magnitude;if(split==std::string::npos||!parse_voltage_number(s.substr(5,split-5),magnitude)||!parse_voltage_number(s.substr(split+2),high)||magnitude<=0)return false;low=-magnitude;return low<high;}if(s.find("v_")==0){size_t split=s.find("to",2);if(split==std::string::npos||!parse_voltage_number(s.substr(2,split-2),low)||!parse_voltage_number(s.substr(split+2),high))return false;return low<high;}if(!s.empty()&&s[s.size()-1]=='v')s.erase(s.size()-1);if(s.find("+/-")==0){double magnitude;if(!parse_voltage_number(s.substr(3),magnitude)||magnitude<=0)return false;low=-magnitude;high=magnitude;return true;}size_t split=s.find('~');if(split==std::string::npos||s.find('~',split+1)!=std::string::npos)return false;std::string left=s.substr(0,split),right=s.substr(split+1);if(!left.empty()&&left[left.size()-1]=='v')left.erase(left.size()-1);if(!right.empty()&&right[right.size()-1]=='v')right.erase(right.size()-1);return parse_voltage_number(left,low)&&parse_voltage_number(right,high)&&low<high;}
inline bool equivalent_voltage_range(const std::string& left,const std::string& right)
{double ll,lh,rl,rh;if(!voltage_range_endpoints(left,ll,lh)||!voltage_range_endpoints(right,rl,rh))return left==right;const double tolerance=1e-9;return std::fabs(ll-rl)<=tolerance&&std::fabs(lh-rh)<=tolerance;}
inline bool equivalent_voltage_ranges(const std::vector<std::string>& left,const std::vector<std::string>& right)
{if(left.size()!=right.size())return false;for(size_t i=0;i<left.size();++i)if(!equivalent_voltage_range(left[i],right[i]))return false;return true;}
}
