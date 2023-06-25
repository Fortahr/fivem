#pragma once

#include <StdInc.h>
#include <Resource.h>

#include "ExternalFunctions/ExternalManager.h"

namespace ExternalFunctions
{
	inline ExternalManager<IS_FXSERVER>& ExternalFunctions::GetManager()
	{
		return manager<IS_FXSERVER>;
	}
}

static InitFunction initFunction([]()
{
	fx::Resource::OnInitializeInstance.Connect([](fx::Resource* resource)
	{
		resource->OnStop.Connect([resource]() { manager<IS_FXSERVER>.OnResourceStop(resource); });
	});

	//m_clientRegistry = instance->GetComponent<ClientRegistry>().GetRef();
});
