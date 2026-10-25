#pragma once

#include <ntddk.h>

// 1단계 진단용 로그. 디버거 필터 설정 없이도 보이도록 ERROR 레벨로 출력한다.
#define CUE_LOG(format, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "CueMidiFilter: " format "\n", ##__VA_ARGS__)
