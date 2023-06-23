#pragma once

#include "AsyncResultId.h"
#include "Binding.h"

class IScriptRuntimeExternalFunctions;

namespace ExternalFunctions
{
	struct ExportFunction
	{
	public:
		IScriptRuntimeExternalFunctions& runtime;
		Binding binding;
		PrivateId privateId;

		ExportFunction(IScriptRuntimeExternalFunctions& runtime, Binding binding, PrivateId privateId)
			: runtime(runtime)
			, binding(binding)
			, privateId(privateId)
		{ }
	};
}
