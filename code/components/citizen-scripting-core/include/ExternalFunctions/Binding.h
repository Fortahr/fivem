#pragma once

namespace ExternalFunctions
{
	enum Binding
	{
		NONE = 0,
		LOCAL = 1 << 0,
		REMOTE = 1 << 1,

		ASYNC = 1 << 2,
	};
}
