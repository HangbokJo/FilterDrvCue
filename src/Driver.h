#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbdi.h>

#include "Trace.h"

// 장치별 로그 횟수 제한용 카운터. 요청은 병렬로 들어오므로 Interlocked로만 갱신한다.
struct FILTER_CONTEXT
{
    volatile LONG DumpCount;             // 설정 디스크립터 완료 로그/덤프
    volatile LONG ConfigRequestLogCount; // 설정 디스크립터 요청 로그
    volatile LONG RequestLogCount;       // 그 외 요청 로그
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILTER_CONTEXT, GetFilterContext)

extern "C"
{
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL EvtIoInternalDeviceControl;
EVT_WDF_REQUEST_COMPLETION_ROUTINE EvtConfigDescriptorCompleted;
}
