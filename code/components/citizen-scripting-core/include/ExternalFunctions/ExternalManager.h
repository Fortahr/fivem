#pragma once

#include <queue>
#include <string_view>
#include <unordered_map>

#include "Stdinc.h"

#include "AsyncAwaiter.h"
#include "AsyncResultId.h"
#include "Binding.h"
#include "Bookkeeping.h"
#include "ExportFunction.h"
#include "IScriptRuntimeExternalFunctions.h"
#include "StatusCode.h"

#ifdef COMPILING_CITIZEN_SCRIPTING_CORE
#define FX_EXTERNAL_FUNCTIONS_LINKAGE DLL_EXPORT
#else
#define FX_EXTERNAL_FUNCTIONS_LINKAGE DLL_IMPORT
#endif

namespace ExternalFunctions
{
	using Runtime = IScriptRuntimeExternalFunctions;

	template<bool _IsServer>
	class ExternalManager
	{
	public:
		typedef std::conditional_t<_IsServer, uint16_t, bool> Remote;

	private:
		std::unordered_map<Runtime*, Bookkeeping::Node*> runtimes;
		std::unordered_map<Remote, std::pair<Bookkeeping::Node*, std::atomic<uint16_t>>> remotes;

		std::unordered_map<std::string, std::unordered_map<std::string, ExportFunction>> exports;
		std::unordered_map<AsyncResultId, AsyncAwaiter> awaiters;
		std::atomic<uint16_t> asyncIds;

		AsyncResultId CreateAsyncId(Remote remote);
		bool IsRemote(Remote remote);

	public:
		// testing
		typedef std::function<void(size_t remote, std::string_view resource, std::string_view exportName, std::string_view message, AsyncResultId asyncResultId)> TestRemoteCall;
		TestRemoteCall testExportSend;

		typedef std::function<void(size_t remote, StatusCode statusCode, std::string_view message, AsyncResultId asyncResultId)> TestRemoteAsync;
		TestRemoteAsync testExportAsync;

		std::queue<std::tuple<AsyncResultId, StatusCode, std::string>> asyncTest;

	public:
		bool RegisterExport(Runtime& runtime, std::string_view resource, std::string_view exportName, PrivateId privateId, Binding binding);

		void InvokeExportFromRemote(Remote remote, std::string_view resource, std::string_view exportName, std::string_view argumentData, AsyncResultId asyncResultId);
		StatusCode InvokeExportFromLocal(Remote remote, std::string_view resource, std::string_view exportName, std::string_view argumentData, const char*& resultData, size_t& resultSize, Runtime& caller, AsyncResultId& asyncResultId);

		void OnDisconnect(Remote remote);
		void OnScriptRuntimeStop(Runtime* runtime);

		void AsyncResultFromLocal(AsyncResultId asyncResultId, StatusCode statusCode, std::string_view argumentData);
		void AsyncResultFromRemote(AsyncResultId asyncResultId, Remote remote, StatusCode statusCode, std::string_view argumentData);
	};

	template <bool _IsServer>
	inline static ExternalManager<IS_FXSERVER> manager;

	FX_EXTERNAL_FUNCTIONS_LINKAGE ExternalManager<IS_FXSERVER>& GetManager();
}

#include "ExternalManager.inl"
