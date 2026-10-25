#include "Driver.h"

namespace
{
constexpr LONG kMaxDumps = 8;
constexpr LONG kMaxConfigRequestLogs = 8;
constexpr LONG kMaxRequestLogs = 64;
constexpr ULONG kMaxDumpBytes = 1024;
constexpr ULONG kBytesPerLine = 16;
constexpr char kHexDigits[] = "0123456789ABCDEF";

// 카운터가 한도에 닿으면 더 올리지 않는다(오버플로로 로그가 다시 쏟아지는 것을 막는다).
bool TryAcquireLogSlot(_Inout_ volatile LONG* counter, _In_ LONG limit)
{
    LONG current = *counter;
    while (current < limit)
    {
        const LONG previous = InterlockedCompareExchange(counter, current + 1, current);
        if (previous == current)
        {
            return true;
        }
        current = previous;
    }
    return false;
}

// GET_DESCRIPTOR(설정 디스크립터) 표준 요청인지 setup 패킷으로 판별한다.
bool IsConfigDescriptorSetup(_In_reads_(8) const UCHAR* setup)
{
    return setup[0] == 0x80 &&
           setup[1] == USB_REQUEST_GET_DESCRIPTOR &&
           setup[3] == USB_CONFIGURATION_DESCRIPTOR_TYPE;
}

bool IsConfigDescriptorRequest(_In_ PURB urb)
{
    switch (urb->UrbHeader.Function)
    {
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
        return urb->UrbControlDescriptorRequest.DescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE;
    case URB_FUNCTION_CONTROL_TRANSFER:
        return IsConfigDescriptorSetup(urb->UrbControlTransfer.SetupPacket);
    case URB_FUNCTION_CONTROL_TRANSFER_EX:
        return IsConfigDescriptorSetup(urb->UrbControlTransferEx.SetupPacket);
    default:
        return false;
    }
}

// IOCTL_INTERNAL_USB_SUBMIT_URB 요청이면 URB를, 아니면 nullptr을 돌려준다.
PURB GetSubmittedUrb(_In_ WDFREQUEST request, _In_ ULONG ioControlCode)
{
    if (ioControlCode != IOCTL_INTERNAL_USB_SUBMIT_URB)
    {
        return nullptr;
    }

    WDF_REQUEST_PARAMETERS params;
    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(request, &params);
    return static_cast<PURB>(params.Parameters.Others.Arg1);
}

struct TransferView
{
    const UCHAR* Data;
    ULONG Length;
};

// 전송 버퍼 위치는 요청 종류마다 다르다. USB 스택은 MDL이 있으면 MDL을 쓰므로 MDL을 우선한다.
// 매핑할 수 없으면 Data는 nullptr이다.
TransferView MakeTransferView(_In_opt_ PVOID buffer, _In_opt_ PMDL mdl, _In_ ULONG length)
{
    if (mdl != nullptr)
    {
        const ULONG mdlBytes = MmGetMdlByteCount(mdl);
        PVOID mapped = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
        return TransferView{ static_cast<const UCHAR*>(mapped), (length < mdlBytes) ? length : mdlBytes };
    }
    return TransferView{ static_cast<const UCHAR*>(buffer), length };
}

TransferView GetTransferView(_In_ PURB urb)
{
    switch (urb->UrbHeader.Function)
    {
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
        return MakeTransferView(urb->UrbControlDescriptorRequest.TransferBuffer,
                                urb->UrbControlDescriptorRequest.TransferBufferMDL,
                                urb->UrbControlDescriptorRequest.TransferBufferLength);
    case URB_FUNCTION_CONTROL_TRANSFER:
        return MakeTransferView(urb->UrbControlTransfer.TransferBuffer,
                                urb->UrbControlTransfer.TransferBufferMDL,
                                urb->UrbControlTransfer.TransferBufferLength);
    case URB_FUNCTION_CONTROL_TRANSFER_EX:
        return MakeTransferView(urb->UrbControlTransferEx.TransferBuffer,
                                urb->UrbControlTransferEx.TransferBufferMDL,
                                urb->UrbControlTransferEx.TransferBufferLength);
    default:
        return TransferView{ nullptr, 0 };
    }
}

// 바이트를 "XX XX ..." 형태의 NUL 종료 문자열로 바꾼다.
void FormatHexLine(_Out_writes_z_(kBytesPerLine * 3 + 1) char* line,
                   _In_reads_(count) const UCHAR* data,
                   _In_range_(1, kBytesPerLine) ULONG count)
{
    for (ULONG i = 0; i < count; ++i)
    {
        line[i * 3] = kHexDigits[data[i] >> 4];
        line[i * 3 + 1] = kHexDigits[data[i] & 0x0F];
        line[i * 3 + 2] = ' ';
    }
    line[count * 3] = '\0';
}

void DumpHex(_In_reads_(length) const UCHAR* data, _In_ ULONG length)
{
    ULONG dumpLength = length;
    if (dumpLength > kMaxDumpBytes)
    {
        dumpLength = kMaxDumpBytes;
        CUE_LOG("  dump truncated: %lu of %lu bytes", dumpLength, length);
    }

    for (ULONG offset = 0; offset < dumpLength; offset += kBytesPerLine)
    {
        char line[kBytesPerLine * 3 + 1];
        const ULONG remaining = dumpLength - offset;
        FormatHexLine(line, data + offset, (remaining < kBytesPerLine) ? remaining : kBytesPerLine);

        CUE_LOG("  %04X: %s", offset, line);
    }
}

// 9바이트 선행 요청과 전체 요청을 구분할 수 있도록 wTotalLength와 실제 길이를 함께 남긴다.
void LogConfigHeader(_In_reads_(length) const UCHAR* data, _In_ ULONG length)
{
    if (length < sizeof(USB_CONFIGURATION_DESCRIPTOR))
    {
        CUE_LOG("  response shorter than a configuration descriptor header: %lu bytes", length);
        return;
    }

    const auto* header = reinterpret_cast<const USB_CONFIGURATION_DESCRIPTOR*>(data);
    CUE_LOG("  wTotalLength=%u, received=%lu (%s), bNumInterfaces=%u",
            header->wTotalLength, length, (length >= header->wTotalLength) ? "complete" : "partial",
            header->bNumInterfaces);
}

void LogUrbSetup(_In_reads_(8) const UCHAR* s)
{
    CUE_LOG("  setup=%02X %02X %02X %02X %02X %02X %02X %02X", s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
}

// "usbaudio가 다른 경로로 디스크립터를 읽는가"를 판별할 수 있도록 GET_DESCRIPTOR 계열은 상세를 남긴다.
void LogUrbDetails(_In_ PURB urb)
{
    switch (urb->UrbHeader.Function)
    {
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
        CUE_LOG("  descriptor type=0x%02X, index=%u, langId=0x%04X, length=%lu",
                urb->UrbControlDescriptorRequest.DescriptorType, urb->UrbControlDescriptorRequest.Index,
                urb->UrbControlDescriptorRequest.LanguageId, urb->UrbControlDescriptorRequest.TransferBufferLength);
        break;
    case URB_FUNCTION_CONTROL_TRANSFER:
        LogUrbSetup(urb->UrbControlTransfer.SetupPacket);
        break;
    case URB_FUNCTION_CONTROL_TRANSFER_EX:
        LogUrbSetup(urb->UrbControlTransferEx.SetupPacket);
        break;
    default:
        break;
    }
}

void LogRequest(_Inout_ FILTER_CONTEXT* context, _In_ ULONG ioControlCode, _In_opt_ PURB urb)
{
    if (!TryAcquireLogSlot(&context->RequestLogCount, kMaxRequestLogs))
    {
        return;
    }

    if (urb != nullptr)
    {
        CUE_LOG("URB function 0x%04X", urb->UrbHeader.Function);
        LogUrbDetails(urb);
    }
    else
    {
        CUE_LOG("internal IOCTL 0x%08X", ioControlCode);
    }
}

void LogConfigDescriptorRequest(_Inout_ FILTER_CONTEXT* context, _In_ PURB urb)
{
    if (TryAcquireLogSlot(&context->ConfigRequestLogCount, kMaxConfigRequestLogs))
    {
        CUE_LOG("config descriptor request: function=0x%04X", urb->UrbHeader.Function);
        LogUrbDetails(urb);
    }
}

void DumpConfigDescriptor(_Inout_ FILTER_CONTEXT* context, _In_ PURB urb)
{
    if (!TryAcquireLogSlot(&context->DumpCount, kMaxDumps))
    {
        return;
    }

    const TransferView view = GetTransferView(urb);
    CUE_LOG("config descriptor completed: usbdStatus=0x%08X, function=0x%04X, length=%lu",
            urb->UrbHeader.Status, urb->UrbHeader.Function, view.Length);

    if (view.Data == nullptr || view.Length == 0)
    {
        CUE_LOG("  config descriptor buffer is not accessible");
        return;
    }

    LogConfigHeader(view.Data, view.Length);
    DumpHex(view.Data, view.Length);
}

void LogConfigDescriptorFailure(_Inout_ FILTER_CONTEXT* context, _In_ NTSTATUS status, _In_ PURB urb)
{
    if (TryAcquireLogSlot(&context->DumpCount, kMaxDumps))
    {
        CUE_LOG("config descriptor request failed: status=0x%08X, usbdStatus=0x%08X", status, urb->UrbHeader.Status);
    }
}

void ForwardAndForget(_In_ WDFDEVICE device, _In_ WDFREQUEST request)
{
    WdfRequestFormatRequestUsingCurrentType(request);

    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    if (!WdfRequestSend(request, WdfDeviceGetIoTarget(device), &options))
    {
        WdfRequestComplete(request, WdfRequestGetStatus(request));
    }
}

void ForwardWithCompletion(_In_ WDFDEVICE device, _In_ WDFREQUEST request, _In_ PURB urb)
{
    WdfRequestFormatRequestUsingCurrentType(request);
    // URB는 요청이 완료될 때까지 호출자가 소유하므로 컨텍스트로 넘겨도 안전하다.
    WdfRequestSetCompletionRoutine(request, EvtConfigDescriptorCompleted, urb);

    if (!WdfRequestSend(request, WdfDeviceGetIoTarget(device), WDF_NO_SEND_OPTIONS))
    {
        WdfRequestComplete(request, WdfRequestGetStatus(request));
    }
}
}

_Use_decl_annotations_
VOID EvtIoInternalDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t /*OutputBufferLength*/,
                                size_t /*InputBufferLength*/, ULONG IoControlCode)
{
    const WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    FILTER_CONTEXT* context = GetFilterContext(device);
    PURB urb = GetSubmittedUrb(Request, IoControlCode);

    if (urb != nullptr && IsConfigDescriptorRequest(urb))
    {
        LogConfigDescriptorRequest(context, urb);
        ForwardWithCompletion(device, Request, urb);
        return;
    }

    LogRequest(context, IoControlCode, urb);
    ForwardAndForget(device, Request);
}

_Use_decl_annotations_
VOID EvtConfigDescriptorCompleted(WDFREQUEST Request, WDFIOTARGET Target, PWDF_REQUEST_COMPLETION_PARAMS Params,
                                  WDFCONTEXT Context)
{
    PURB urb = static_cast<PURB>(Context);
    FILTER_CONTEXT* context = GetFilterContext(WdfIoTargetGetDevice(Target));

    if (NT_SUCCESS(Params->IoStatus.Status) && USBD_SUCCESS(urb->UrbHeader.Status))
    {
        DumpConfigDescriptor(context, urb);
    }
    else
    {
        LogConfigDescriptorFailure(context, Params->IoStatus.Status, urb);
    }

    // 1단계는 관찰만 한다. 응답은 수정하지 않고 그대로 완료한다.
    WdfRequestComplete(Request, Params->IoStatus.Status);
}
