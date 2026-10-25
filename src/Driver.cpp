#include "Driver.h"

#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, EvtDeviceAdd)

namespace
{
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS CreateFilterQueue(_In_ WDFDEVICE device)
{
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoInternalDeviceControl = EvtIoInternalDeviceControl;

    return WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}
}

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);

    NTSTATUS status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
    {
        CUE_LOG("WdfDriverCreate failed: 0x%08X", status);
    }
    return status;
}

_Use_decl_annotations_
NTSTATUS EvtDeviceAdd(WDFDRIVER /*Driver*/, PWDFDEVICE_INIT DeviceInit)
{
    PAGED_CODE();

    WdfFdoInitSetFilter(DeviceInit);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FILTER_CONTEXT);
    // URB는 DISPATCH_LEVEL에서 올 수 있으므로, 기본값에 기대지 않고 명시적으로 고정한다.
    attributes.ExecutionLevel = WdfExecutionLevelDispatch;

    WDFDEVICE device;
    NTSTATUS status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        CUE_LOG("WdfDeviceCreate failed: 0x%08X", status);
        return status;
    }

    status = CreateFilterQueue(device);
    if (!NT_SUCCESS(status))
    {
        CUE_LOG("WdfIoQueueCreate failed: 0x%08X", status);
        return status;
    }

    CUE_LOG("device added");
    return STATUS_SUCCESS;
}
