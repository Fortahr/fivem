#pragma once

#include <cstdint>
#include <string_view>

#ifndef __has_include("fxIBase.h")
#include "fxIBase.h"
#endif

#include "AsyncResultId.h"
#include "StatusCode.h"

using namespace ExternalFunctions;

/* starting interface:    ISCRIPTRUNTIME_EXTERNALFUNCTIONS */
#define ISCRIPTRUNTIME_EXTERNALFUNCTIONS_IID_STR "bef565ee-61b7-4543-990a-5fbb9f7814a3"

#define ISCRIPTRUNTIME_EXTERNALFUNCTIONS_IID \
  { 0xbef565ee, 0x61b7, 0x4543, \
    { 0x99, 0x0a, 0x5f, 0xbb, 0x9f, 0x78, 0x14, 0xa3 }}

class NS_NO_VTABLE IScriptRuntimeExternalFunctions : public fxIBase
{
public:
	NS_DECLARE_STATIC_IID_ACCESSOR(ISCRIPTRUNTIME_EXTERNALFUNCTIONS_IID)

	NS_IMETHOD_(StatusCode) InvokeExport(PrivateId privateId, std::string_view argumentData, const char*& resultData, size_t& resultSize, AsyncResultId asyncResultId) = 0;
	NS_IMETHOD_(void) AsyncResult(PrivateId privateId, std::string_view argumentData, AsyncResultId asyncResultId) = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(IScriptRuntimeExternalFunctions, ISCRIPTRUNTIME_EXTERNALFUNCTIONS_IID)

/* Use this macro when declaring classes that implement this interface. */
#define NS_DECL_ISCRIPTRUNTIME_EXTERNALFUNCTIONS \
	NS_IMETHOD_(StatusCode) InvokeExport(PrivateId privateId, std::string_view argumentData, const char*& resultData, size_t& resultSize, AsyncResultId asyncResultId) override; \
	NS_IMETHOD_(void) AsyncResult(PrivateId privateId, std::string_view argumentData, AsyncResultId asyncResultId) override;
