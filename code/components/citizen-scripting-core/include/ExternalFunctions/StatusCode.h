#pragma once

namespace ExternalFunctions
{
	enum class StatusCode
	{
		SUCCESSFUL,
		ASYNC,
		REMOTE,

		FUNCTION_INACCASSIBLE,
		FUNCTION_NOT_FOUND,
		RESOURCE_NOT_FOUND,
		FAILED,
	};
}