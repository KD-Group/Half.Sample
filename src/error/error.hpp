#ifndef ERROR_HPP
#define ERROR_HPP

#include <string>

namespace Error {

    enum Code {
        SUCCESS,

        // error of human
        COMMAND_NOT_FOUND,
        SAMPLER_NOT_FOUND,
        NOW_IN_MEASURING,
        FILE_NOT_FOUND,

        // error of sampling
        VOLTAGE_NOT_ENOUGH,
        WAVE_NOT_FOUND,
        APPROPRIATE_WAVE_NOT_FOUND,

        // error of original sampling
        ///************************************************************************
        /// warning
        ///************************************************************************
        /// <summary>
        /// The interrupt resource is not available.
        /// </summary>
        WarningIntrNotAvailable = 0xA0000000,

        /// <summary>
        /// The parameter is out of the range.
        /// </summary>
        WarningParamOutOfRange = 0xA0000001,

        /// <summary>
        /// The property value is out of range.
        /// </summary>
        WarningPropValueOutOfRange = 0xA0000002,

        /// <summary>
        /// The property value is not supported.
        /// </summary>
        WarningPropValueNotSpted = 0xA0000003,

        /// <summary>
        /// The property value conflicts with the current state.
        /// </summary>
        WarningPropValueConflict = 0xA0000004,

        /// <summary>
        /// The value range of all channels in a group should be same,
        /// such as 4~20mA of PCI-1724.
        /// </summary>
        WarningVrgOfGroupNotSame = 0xA0000005,

        ///***********************************************************************
        /// error
        ///***********************************************************************
        /// <summary>
        /// The handle is NULL or its type doesn't match the required operation.
        /// </summary>
        ErrorHandleNotValid = 0xE0000000,

        /// <summary>
        /// The parameter value is out of range.
        /// </summary>
        ErrorParamOutOfRange = 0xE0000001,

        /// <summary>
        /// The parameter value is not supported.
        /// </summary>
        ErrorParamNotSpted = 0xE0000002,

        /// <summary>
        /// The parameter value format is not the expected.
        /// </summary>
        ErrorParamFmtUnexpted = 0xE0000003,

        /// <summary>
        /// Not enough memory is available to complete the operation.
        /// </summary>
        ErrorMemoryNotEnough = 0xE0000004,

        /// <summary>
        /// The data buffer is null.
        /// </summary>
        ErrorBufferIsNull = 0xE0000005,

        /// <summary>
        /// The data buffer is too small for the operation.
        /// </summary>
        ErrorBufferTooSmall = 0xE0000006,

        /// <summary>
        /// The data length exceeded the limitation.
        /// </summary>
        ErrorDataLenExceedLimit = 0xE0000007,

        /// <summary>
        /// The required function is not supported.
        /// </summary>
        ErrorFuncNotSpted = 0xE0000008,

        /// <summary>
        /// The required event is not supported.
        /// </summary>
        ErrorEventNotSpted = 0xE0000009,

        /// <summary>
        /// The required property is not supported.
        /// </summary>
        ErrorPropNotSpted = 0xE000000A,

        /// <summary>
        /// The required property is read-only.
        /// </summary>
        ErrorPropReadOnly = 0xE000000B,

        /// <summary>
        /// The specified property value conflicts with the current state.
        /// </summary>
        ErrorPropValueConflict = 0xE000000C,

        /// <summary>
        /// The specified property value is out of range.
        /// </summary>
        ErrorPropValueOutOfRange = 0xE000000D,

        /// <summary>
        /// The specified property value is not supported.
        /// </summary>
        ErrorPropValueNotSpted = 0xE000000E,

        /// <summary>
        /// The handle hasn't own the privilege of the operation the user wanted.
        /// </summary>
        ErrorPrivilegeNotHeld = 0xE000000F,

        /// <summary>
        /// The required privilege is not available because someone else had own it.
        /// </summary>
        ErrorPrivilegeNotAvailable = 0xE0000010,

        /// <summary>
        /// The driver of specified device was not found.
        /// </summary>
        ErrorDriverNotFound = 0xE0000011,

        /// <summary>
        /// The driver version of the specified device mismatched.
        /// </summary>
        ErrorDriverVerMismatch = 0xE0000012,

        /// <summary>
        /// The loaded driver count exceeded the limitation.
        /// </summary>
        ErrorDriverCountExceedLimit = 0xE0000013,

        /// <summary>
        /// The device is not opened.
        /// </summary>
        ErrorDeviceNotOpened = 0xE0000014,

        /// <summary>
        /// The required device does not exist.
        /// </summary>
        ErrorDeviceNotExist = 0xE0000015,

        /// <summary>
        /// The required device is unrecognized by driver.
        /// </summary>
        ErrorDeviceUnrecognized = 0xE0000016,

        /// <summary>
        /// The configuration data of the specified device is lost or unavailable.
        /// </summary>
        ErrorConfigDataLost = 0xE0000017,

        /// <summary>
        /// The function is not initialized and can't be started.
        /// </summary>
        ErrorFuncNotInited = 0xE0000018,

        /// <summary>
        /// The function is busy.
        /// </summary>
        ErrorFuncBusy = 0xE0000019,

        /// <summary>
        /// The interrupt resource is not available.
        /// </summary>
        ErrorIntrNotAvailable = 0xE000001A,

        /// <summary>
        /// The DMA channel is not available.
        /// </summary>
        ErrorDmaNotAvailable = 0xE000001B,

        /// <summary>
        /// Time out when reading/writing the device.
        /// </summary>
        ErrorDeviceIoTimeOut = 0xE000001C,

        /// <summary>
        /// The given signature does not match with the device current one.
        /// </summary>
        ErrorSignatureNotMatch = 0xE000001D,

        /// <summary>
        /// The function cannot be executed while the buffered AI is running.
        /// </summary>
        ErrorFuncConflictWithBfdAi = 0xE000001E,

        /// <summary>
        /// The value range is not available in single-ended mode.
        /// </summary>
        ErrorVrgNotAvailableInSeMode = 0xE000001F,

        /// <summary>
        /// Undefined error
        /// </summary>
        ErrorUndefined = 0xE000FFFF,
    };

    std::string to_string(Code error_code);

} // namespace Error

#endif
