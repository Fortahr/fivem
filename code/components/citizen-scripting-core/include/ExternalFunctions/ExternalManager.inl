#pragma once

#include <cassert>
#include <chrono>
#include <string_view>

#include <Resource.h>

#include "ExternalManager.h"

using namespace std::string_view_literals;

namespace ExternalFunctions
{
	template<bool _IsServer>
	inline AsyncResultId ExternalManager<_IsServer>::CreateAsyncId(Remote remote)
	{
		return AsyncResultId(remote,
			remote ? remotes[remote].second++ : asyncIds++,
			(uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	}

	template<bool _IsServer>
	inline AsyncResultId ExternalManager<_IsServer>::CreateLocalAsyncId()
	{
		auto timestamp = (decltype(AsyncResultId::timestamp))std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

		if constexpr (_IsServer)
			return AsyncResultId(AsyncResultId::SERVER, asyncIds++, timestamp);
		else
			return AsyncResultId(0, asyncIds++, timestamp);
	}

	template<bool _IsServer>
	inline bool ExternalManager<_IsServer>::IsRemote(Remote remote)
	{
		if constexpr (_IsServer)
			return remote != AsyncResultId::SERVER;
		else
			return remote;
	}

	template<bool _IsServer>
	inline bool ExternalManager<_IsServer>::RegisterExport(Runtime& runtime, std::string_view exportName, PrivateId privateId, Binding binding)
	{
		auto inserted = exports[runtime.GetResource()->GetName()].try_emplace((std::string)exportName, runtime, binding, privateId);
		return inserted.second;
	}

	template<bool _IsServer>
	inline void ExternalManager<_IsServer>::InvokeExportFromRemote(Remote remote, std::string_view resource, std::string_view exportName, std::string_view argumentData, AsyncResultId asyncResultId)
	{
		asyncResultId.remote = remote;

		StatusCode status;
		std::string_view resultData;

		auto resourceFound = exports.find((std::string)resource);
		if (resourceFound != exports.end())
		{
			auto exportFound = resourceFound->second.find((std::string)exportName);
			if (exportFound != resourceFound->second.end())
			{
				Bookkeeping::ExportFunction& exportFunc = exportFound->second;

				status = exportFunc.binding & Binding::LOCAL
					? exportFunc.resource.CallExport(exportFound->second.privateId, argumentData, &resultData, asyncResultId, asyncTest)
					: StatusCode::FUNCTION_INACCASSIBLE;
			}
			else
				status = StatusCode::FUNCTION_NOT_FOUND;
		}
		else
			status = StatusCode::RESOURCE_NOT_FOUND;

		if (status != StatusCode::ASYNC)
		{
			testExportAsync(remote, status, resultData, asyncResultId);
		}
	}

	template<bool _IsServer>
	inline StatusCode ExternalManager<_IsServer>::InvokeExportFromLocal(Remote remote, const std::string& resource, std::string_view exportName, std::string_view argumentData, const char*& resultData, size_t& resultSize, Runtime& caller, AsyncResultId& asyncResultId)
	{
		if (IsRemote(remote))
		{
			auto resultId = asyncResultId = CreateAsyncId(remote);
			auto awaiter = awaiters.try_emplace(resultId, caller, awaiters, resultId);
			if (awaiter.second)
			{
				awaiter.first->second.AddToList(Bookkeeping::Sources::RESOURCE_CALLER, &resourceCallers[caller.GetResource()]);
				awaiter.first->second.AddToList(Bookkeeping::Sources::RESOURCE_CALLEE, &resourceCallees[resource]);
				awaiter.first->second.AddToList(Bookkeeping::Sources::REMOTE, &remotes[remote].first);
			}

			testExportSend(remote, resource, exportName, argumentData, resultId);

			return StatusCode::ASYNC;
		}
		else
		{
			auto resourceFound = exports.find(resource);
			if (resourceFound != exports.end())
			{
				auto exportFound = resourceFound->second.find((std::string)exportName);
				if (exportFound != resourceFound->second.end())
				{
					ExportFunction& exportFunc = exportFound->second;
					if (exportFunc.binding & Binding::LOCAL)
					{
						auto resultId = asyncResultId = CreateLocalAsyncId();
						StatusCode status = exportFunc.runtime.InvokeExport(exportFound->second.privateId, argumentData, resultData, resultSize, asyncResultId);
						if (status == StatusCode::ASYNC)
						{
							auto awaiter = awaiters.try_emplace(resultId, caller, awaiters, resultId);
							if (awaiter.second)
							{
								awaiter.first->second.AddToList(Bookkeeping::Sources::RESOURCE_CALLER, &resourceCallers[caller.GetResource()]);
								awaiter.first->second.AddToList(Bookkeeping::Sources::RESOURCE_CALLEE, &resourceCallees[resource]);
							}
						}

						return status;
					}

					return StatusCode::FUNCTION_INACCASSIBLE;
				}

				return StatusCode::FUNCTION_NOT_FOUND;
			}

			return StatusCode::RESOURCE_NOT_FOUND;
		}
	}

	template<bool _IsServer>
	inline void ExternalManager<_IsServer>::AsyncResultFromLocal(AsyncResultId asyncResultId, StatusCode statusCode, std::string_view argumentData)
	{
		if (IsRemote(asyncResultId.remote))
		{
			testExportAsync(asyncResultId.remote, statusCode, argumentData, asyncResultId);
		}
		else
		{
			auto found = awaiters.find(asyncResultId);
			if (found != awaiters.end())
			{
				auto& awaiter = found->second;
				awaiter.runtime.AsyncResult(asyncResultId, (uint8_t)statusCode, argumentData);
				awaiters.erase(found);
			}
		}
	}

	template<bool _IsServer>
	inline void ExternalManager<_IsServer>::AsyncResultFromRemote(AsyncResultId asyncResultId, Remote remote, StatusCode statusCode, std::string_view argumentData)
	{
		if constexpr (_IsServer)
			asyncResultId.remote = remote;
		else
			asyncResultId.remote = true;

		auto found = awaiters.find(asyncResultId);
		if (found != awaiters.end())
		{
			auto& awaiter = found->second;
			aawaiter.runtime.AsyncResult(asyncResultId, statusCode, argumentData);
			awaiters.erase(found);
		}
	}

	template<bool _IsServer>
	inline void ExternalManager<_IsServer>::OnDisconnect(Remote remote)
	{
		auto found = remotes.find(remote);
		if (found != remotes.end())
		{
			for (Bookkeeping::Node* node = found->second; node; )
			{
				auto next = node->links[(size_t)Bookkeeping::Sources::REMOTE].next;
				node->Erase();
				node = next;
			}

			if (!found->second)
				remotes.erase(found);
		}
	}

	template<bool _IsServer>
	inline void ExternalManager<_IsServer>::OnResourceStop(fx::Resource* resource)
	{
		auto removedCount = exports.erase(resource->GetName());
		trace("OnResourceStop %s %d\n", resource->GetName(), removedCount);

		// fail any that awaits this resource
		{
			auto found = resourceCallees.find(resource->GetName());
			if (found != resourceCallees.end())
			{
				for (Bookkeeping::Node* node = found->second; node;)
				{
					auto next = node->links[(size_t)Bookkeeping::Sources::RESOURCE_CALLEE].next;

					auto asyncResultId = node->GetAsyncResultId();
					auto foundAwaiter = awaiters.find(asyncResultId);
					if (foundAwaiter != awaiters.end())
					{
						Runtime& runtime = foundAwaiter->second.runtime;
						runtime.AsyncResult(asyncResultId, (uint8_t)StatusCode::FAILED, "\091\0C0"sv);
						awaiters.erase(foundAwaiter);
					}

					node = next;
				}

				resourceCallees.erase(found);
			}
		}

		// remove all of this resource
		{
			auto found = resourceCallers.find(resource);
			if (found != resourceCallers.end())
			{
				for (Bookkeeping::Node* node = found->second; node;)
				{
					auto next = node->links[(size_t)Bookkeeping::Sources::RESOURCE_CALLER].next;
					awaiters.erase(node->GetAsyncResultId());
					node = next;
				}

				resourceCallers.erase(found);
			}
		}

		trace("\tCallees [%d]\n", resourceCallees.size());
		for (auto& pair : resourceCallees)
		{
			trace("\t\tCallee %s %p\n", pair.first, (void*)pair.second);
		}

		trace("\tCallers [%d]\n", resourceCallers.size());
		for (auto& pair : resourceCallers)
		{
			trace("\t\tCaller %s %p\n", pair.first->GetName(), (void*)pair.second);
		}
	}
}
