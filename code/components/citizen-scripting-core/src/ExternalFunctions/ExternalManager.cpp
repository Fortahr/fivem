#pragma once

#include "StdInc.h"
#include "ExternalFunctions/ExternalManager.h"

namespace ExternalFunctions
{
	inline ExternalManager<IS_FXSERVER>& ExternalFunctions::GetManager()
	{
		return manager<IS_FXSERVER>;
	}
}
