#include "DkSysPort.h"

PCHAR DkDbgGetUSBFunc(USHORT usFuncCode)
{
    PCHAR  pChRes = NULL;

    switch (usFuncCode)
    {
        case URB_FUNCTION_SELECT_CONFIGURATION:
            pChRes = "URB_FUNCTION_SELECT_CONFIGURATION";
            break;
        case URB_FUNCTION_SELECT_INTERFACE:
            pChRes = "URB_FUNCTION_SELECT_INTERFACE";
            break;
        case URB_FUNCTION_ABORT_PIPE:
            pChRes = "URB_FUNCTION_ABORT_PIPE";
            break;
        case URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL:
            pChRes = "URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL";
            break;
        case URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL:
            pChRes = "URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL";
            break;
        case URB_FUNCTION_GET_FRAME_LENGTH:
            pChRes = "URB_FUNCTION_GET_FRAME_LENGTH";
            break;
        case URB_FUNCTION_SET_FRAME_LENGTH:
            pChRes = "URB_FUNCTION_SET_FRAME_LENGTH";
            break;
        case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
            pChRes = "URB_FUNCTION_GET_CURRENT_FRAME_NUMBER";
            break;
        case URB_FUNCTION_CONTROL_TRANSFER:
            pChRes = "URB_FUNCTION_CONTROL_TRANSFER";
            break;
        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
            pChRes = "URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER";
            break;
        case URB_FUNCTION_ISOCH_TRANSFER:
            pChRes = "URB_FUNCTION_ISOCH_TRANSFER";
            break;
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
            pChRes = "URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE";
            break;
        case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
            pChRes = "URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE";
            break;
        case URB_FUNCTION_SET_FEATURE_TO_DEVICE:
            pChRes = "URB_FUNCTION_SET_FEATURE_TO_DEVICE";
            break;
        case URB_FUNCTION_SET_FEATURE_TO_INTERFACE:
            pChRes = "URB_FUNCTION_SET_FEATURE_TO_INTERFACE";
            break;
        case URB_FUNCTION_SET_FEATURE_TO_ENDPOINT:
            pChRes = "URB_FUNCTION_SET_FEATURE_TO_ENDPOINT";
            break;
        case URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE:
            pChRes = "URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE";
            break;
        case URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE:
            pChRes = "URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE";
            break;
        case URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT:
            pChRes = "URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT";
            break;
        case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
            pChRes = "URB_FUNCTION_GET_STATUS_FROM_DEVICE";
            break;
        case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
            pChRes = "URB_FUNCTION_GET_STATUS_FROM_INTERFACE";
            break;
        case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
            pChRes = "URB_FUNCTION_GET_STATUS_FROM_ENDPOINT";
            break;
        case URB_FUNCTION_RESERVED_0X0016:
            pChRes = "URB_FUNCTION_RESERVED_0X0016";
            break;
        case URB_FUNCTION_VENDOR_DEVICE:
            pChRes = "URB_FUNCTION_VENDOR_DEVICE";
            break;
        case URB_FUNCTION_VENDOR_INTERFACE:
            pChRes = "URB_FUNCTION_VENDOR_INTERFACE";
            break;
        case URB_FUNCTION_VENDOR_ENDPOINT:
            pChRes = "URB_FUNCTION_VENDOR_ENDPOINT";
            break;
        case URB_FUNCTION_CLASS_DEVICE:
            pChRes = "URB_FUNCTION_SELECT_CONFIGURATION";
            break;
        case URB_FUNCTION_CLASS_INTERFACE:
            pChRes = "URB_FUNCTION_CLASS_INTERFACE";
            break;
        case URB_FUNCTION_CLASS_ENDPOINT:
            pChRes = "URB_FUNCTION_CLASS_ENDPOINT";
            break;
        case URB_FUNCTION_RESERVE_0X001D:
            pChRes = "URB_FUNCTION_RESERVE_0X001D";
            break;
        case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
            pChRes = "URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL";
            break;
        case URB_FUNCTION_CLASS_OTHER:
            pChRes = "URB_FUNCTION_CLASS_OTHER";
            break;
        case URB_FUNCTION_VENDOR_OTHER:
            pChRes = "URB_FUNCTION_VENDOR_OTHER";
            break;
        case URB_FUNCTION_GET_STATUS_FROM_OTHER:
            pChRes = "URB_FUNCTION_GET_STATUS_FROM_OTHER";
            break;
        case URB_FUNCTION_CLEAR_FEATURE_TO_OTHER:
            pChRes = "URB_FUNCTION_CLEAR_FEATURE_TO_OTHER";
            break;
        case URB_FUNCTION_SET_FEATURE_TO_OTHER:
            pChRes = "URB_FUNCTION_SET_FEATURE_TO_OTHER";
            break;
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
            pChRes = "URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT";
            break;
        case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
            pChRes = "URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT";
            break;
        case URB_FUNCTION_GET_CONFIGURATION:
            pChRes = "URB_FUNCTION_GET_CONFIGURATION";
            break;
        case URB_FUNCTION_GET_INTERFACE:
            pChRes = "URB_FUNCTION_GET_INTERFACE";
            break;
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
            pChRes = "URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE";
            break;
        case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
            pChRes = "URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE";
            break;

        default:
            pChRes = "Unknown usb function code!";
            break;
    }

    return pChRes;
}

PWCHAR DkDbgGetUSBFuncW(USHORT usFuncCode)
{
    PWCHAR  pChRes = NULL;

    switch (usFuncCode)
    {
        case URB_FUNCTION_SELECT_CONFIGURATION:
            pChRes = L"URB_FUNCTION_SELECT_CONFIGURATION";
            break;
        case URB_FUNCTION_SELECT_INTERFACE:
            pChRes = L"URB_FUNCTION_SELECT_INTERFACE";
            break;
        case URB_FUNCTION_ABORT_PIPE:
            pChRes = L"URB_FUNCTION_ABORT_PIPE";
            break;
        case URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL:
            pChRes = L"URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL";
            break;
        case URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL:
            pChRes = L"URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL";
            break;
        case URB_FUNCTION_GET_FRAME_LENGTH:
            pChRes = L"URB_FUNCTION_GET_FRAME_LENGTH";
            break;
        case URB_FUNCTION_SET_FRAME_LENGTH:
            pChRes = L"URB_FUNCTION_SET_FRAME_LENGTH";
            break;
        case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
            pChRes = L"URB_FUNCTION_GET_CURRENT_FRAME_NUMBER";
            break;
        case URB_FUNCTION_CONTROL_TRANSFER:
            pChRes = L"URB_FUNCTION_CONTROL_TRANSFER";
            break;
        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
            pChRes = L"URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER";
            break;
        case URB_FUNCTION_ISOCH_TRANSFER:
            pChRes = L"URB_FUNCTION_ISOCH_TRANSFER";
            break;
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
            pChRes = L"URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE";
            break;
        case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
            pChRes = L"URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE";
            break;
        case URB_FUNCTION_SET_FEATURE_TO_DEVICE:
            pChRes = L"URB_FUNCTION_SET_FEATURE_TO_DEVICE";
            break;
        case URB_FUNCTION_SET_FEATURE_TO_INTERFACE:
            pChRes = L"URB_FUNCTION_SET_FEATURE_TO_INTERFACE";
            break;
        case URB_FUNCTION_SET_FEATURE_TO_ENDPOINT:
            pChRes = L"URB_FUNCTION_SET_FEATURE_TO_ENDPOINT";
            break;
        case URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE:
            pChRes = L"URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE";
            break;
        case URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE:
            pChRes = L"URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE";
            break;
        case URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT:
            pChRes = L"URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT";
            break;
        case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
            pChRes = L"URB_FUNCTION_GET_STATUS_FROM_DEVICE";
            break;
        case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
            pChRes = L"URB_FUNCTION_GET_STATUS_FROM_INTERFACE";
            break;
        case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
            pChRes = L"URB_FUNCTION_GET_STATUS_FROM_ENDPOINT";
            break;
        case URB_FUNCTION_RESERVED_0X0016:
            pChRes = L"URB_FUNCTION_RESERVED_0X0016";
            break;
        case URB_FUNCTION_VENDOR_DEVICE:
            pChRes = L"URB_FUNCTION_VENDOR_DEVICE";
            break;
        case URB_FUNCTION_VENDOR_INTERFACE:
            pChRes = L"URB_FUNCTION_VENDOR_INTERFACE";
            break;
        case URB_FUNCTION_VENDOR_ENDPOINT:
            pChRes = L"URB_FUNCTION_VENDOR_ENDPOINT";
            break;
        case URB_FUNCTION_CLASS_DEVICE:
            pChRes = L"URB_FUNCTION_SELECT_CONFIGURATION";
            break;
        case URB_FUNCTION_CLASS_INTERFACE:
            pChRes = L"URB_FUNCTION_CLASS_INTERFACE";
            break;
        case URB_FUNCTION_CLASS_ENDPOINT:
            pChRes = L"URB_FUNCTION_CLASS_ENDPOINT";
            break;
        case URB_FUNCTION_RESERVE_0X001D:
            pChRes = L"URB_FUNCTION_RESERVE_0X001D";
            break;
        case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
            pChRes = L"URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL";
            break;
        case URB_FUNCTION_CLASS_OTHER:
            pChRes = L"URB_FUNCTION_CLASS_OTHER";
            break;
        case URB_FUNCTION_VENDOR_OTHER:
            pChRes = L"URB_FUNCTION_VENDOR_OTHER";
            break;
        case URB_FUNCTION_GET_STATUS_FROM_OTHER:
            pChRes = L"URB_FUNCTION_GET_STATUS_FROM_OTHER";
            break;
        case URB_FUNCTION_CLEAR_FEATURE_TO_OTHER:
            pChRes = L"URB_FUNCTION_CLEAR_FEATURE_TO_OTHER";
            break;
        case URB_FUNCTION_SET_FEATURE_TO_OTHER:
            pChRes = L"URB_FUNCTION_SET_FEATURE_TO_OTHER";
            break;
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
            pChRes = L"URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT";
            break;
        case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
            pChRes = L"URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT";
            break;
        case URB_FUNCTION_GET_CONFIGURATION:
            pChRes = L"URB_FUNCTION_GET_CONFIGURATION";
            break;
        case URB_FUNCTION_GET_INTERFACE:
            pChRes = L"URB_FUNCTION_GET_INTERFACE";
            break;
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
            pChRes = L"URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE";
            break;
        case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
            pChRes = L"URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE";
            break;

        default:
            pChRes = L"Unknown usb function code!";
            break;
    }

    return pChRes;
}
