#pragma once

#include "Bookkeeping.h"

class IScriptRuntimeExternalFunctions;

namespace ExternalFunctions
{
	struct AsyncAwaiter : Bookkeeping::Node
	{
	public:
		IScriptRuntimeExternalFunctions& runtime;

		AsyncAwaiter(IScriptRuntimeExternalFunctions& runtime, std::unordered_map<AsyncResultId, AsyncAwaiter>& container, AsyncResultId identifier)
			: Bookkeeping::Node(container, identifier)
			, runtime(runtime)
		{ }
	};
}
