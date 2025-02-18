#include "error.hpp"

namespace Error {

    std::string to_string(Code error_code) {
        switch (error_code) {
            case SUCCESS:
                return "success";
            case COMMAND_NOT_FOUND:
                return "command_not_found";
            case SAMPLER_NOT_FOUND:
                return "sampler_not_found";
            case NOW_IN_MEASURING:
                return "now_in_measuring";
            case VOLTAGE_NOT_ENOUGH:
                return "voltage_not_enough";
            case WAVE_NOT_FOUND:
                return "wave_not_found";
            case APPROPRIATE_WAVE_NOT_FOUND:
                return "appropriate_wave_not_found";

            // Warnings (0xA0000000 ~)
            case WarningIntrNotAvailable:
                return "warning_intr_not_available";
            case WarningParamOutOfRange:
                return "warning_param_out_of_range";
            case WarningPropValueOutOfRange:
                return "warning_prop_value_out_of_range";
            case WarningPropValueNotSpted:
                return "warning_prop_value_not_supported";
            case WarningPropValueConflict:
                return "warning_prop_value_conflict";
            case WarningVrgOfGroupNotSame:
                return "warning_vrg_of_group_not_same";

            // Errors (0xE0000000 ~)
            case ErrorHandleNotValid:
                return "error_handle_not_valid";
            case ErrorParamOutOfRange:
                return "error_param_out_of_range";
            case ErrorParamNotSpted:
                return "error_param_not_supported";
            case ErrorParamFmtUnexpted:
                return "error_param_fmt_unexpected";
            case ErrorMemoryNotEnough:
                return "error_memory_not_enough";
            case ErrorBufferIsNull:
                return "error_buffer_is_null";
            case ErrorBufferTooSmall:
                return "error_buffer_too_small";
            case ErrorDataLenExceedLimit:
                return "error_data_len_exceed_limit";
            case ErrorFuncNotSpted:
                return "error_func_not_supported";
            case ErrorEventNotSpted:
                return "error_event_not_supported";
            case ErrorPropNotSpted:
                return "error_prop_not_supported";
            case ErrorPropReadOnly:
                return "error_prop_read_only";
            case ErrorPropValueConflict:
                return "error_prop_value_conflict";
            case ErrorPropValueOutOfRange:
                return "error_prop_value_out_of_range";
            case ErrorPropValueNotSpted:
                return "error_prop_value_not_supported";
            case ErrorPrivilegeNotHeld:
                return "error_privilege_not_held";
            case ErrorPrivilegeNotAvailable:
                return "error_privilege_not_available";
            case ErrorDriverNotFound:
                return "error_driver_not_found";
            case ErrorDriverVerMismatch:
                return "error_driver_ver_mismatch";
            case ErrorDriverCountExceedLimit:
                return "error_driver_count_exceed_limit";
            case ErrorDeviceNotOpened:
                return "error_device_not_opened";
            case ErrorDeviceNotExist:
                return "error_device_not_exist";
            case ErrorDeviceUnrecognized:
                return "error_device_unrecognized";
            case ErrorConfigDataLost:
                return "error_config_data_lost";
            case ErrorFuncNotInited:
                return "error_func_not_inited";
            case ErrorFuncBusy:
                return "error_func_busy";
            case ErrorIntrNotAvailable:
                return "error_intr_not_available";
            case ErrorDmaNotAvailable:
                return "error_dma_not_available";
            case ErrorDeviceIoTimeOut:
                return "error_device_io_time_out";
            case ErrorSignatureNotMatch:
                return "error_signature_not_match";
            case ErrorFuncConflictWithBfdAi:
                return "error_func_conflict_with_bfd_ai";
            case ErrorVrgNotAvailableInSeMode:
                return "error_vrg_not_available_in_se_mode";
            case ErrorUndefined:
                return "error_undefined";

            // Fallback for unknown codes
            default:
                return "error_not_found";
        }
    }

} // namespace Error
