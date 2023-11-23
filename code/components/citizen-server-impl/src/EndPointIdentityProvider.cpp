/*
* This file is part of FiveM: https://fivem.net/
*
* See LICENSE and MENTIONS in the root of the source tree for information
* regarding licensing.
*/

#include "StdInc.h"
#include <ServerIdentityProvider.h>

#include <tbb/concurrent_unordered_map.h>

#include <HttpClient.h>
#include <HttpServer.h>

#include <TcpListenManager.h>
#include <ClientRegistry.h>

static InitFunction initFunction([]()
{
	static struct EndPointIdProvider : public fx::ServerIdentityProviderBase
	{
		tbb::concurrent_unordered_map<std::string, bool> allowedByPolicyCache;

		fx::ClientRegistry* m_registry = nullptr;

		virtual std::string GetIdentifierPrefix() override
		{
			return "ip";
		}

		virtual int GetVarianceLevel() override
		{
			return 5;
		}

		virtual int GetTrustLevel() override
		{
			return 5;
		}

		virtual void RunAuthentication(const fx::ClientSharedPtr& clientPtr, const std::map<std::string, std::string>& postMap, const std::function<void(boost::optional<std::string>)>& cb) override
		{
			const auto& currentTcpEndPoint = clientPtr->GetTcpEndPoint();
			SetClientIdentifier(clientPtr, currentTcpEndPoint, cb);
		}

		void RunRealIPAuthentication(const fx::ClientSharedPtr& clientPtr, const fwRefContainer<net::HttpRequest>& request, const std::map<std::string, std::string>& postMap, const std::function<void(boost::optional<std::string>)>& cb, const std::string& realIP)
		{
			const auto& currentTcpEndPoint = clientPtr->GetTcpEndPoint();
			bool found = fx::IsProxyAddress(currentTcpEndPoint);

			if (!found)
			{
				SetClientIdentifier(clientPtr, currentTcpEndPoint, cb);
			}
			else
			{
				SetClientTcpAddress(clientPtr, realIP, cb);
			}
		}

		virtual void RunAuthentication(const fx::ClientSharedPtr& clientPtr, const fwRefContainer<net::HttpRequest>& request, const std::map<std::string, std::string>& postMap, const std::function<void(boost::optional<std::string>)>& cb) override
		{
			auto sourceIP = request->GetHeader("X-Cfx-Source-Ip", "");
			auto realIP = request->GetHeader("X-Real-Ip", "");

			if (sourceIP.empty() && realIP.empty())
			{
				return RunAuthentication(clientPtr, postMap, cb);
			}

			if (!realIP.empty())
			{
				return RunRealIPAuthentication(clientPtr, request, postMap, cb, realIP);
			}

			auto clep = clientPtr->GetTcpEndPoint();

			auto it = allowedByPolicyCache.find(clep);

			if (it != allowedByPolicyCache.end() && it->second)
			{
				SetClientTcpAddress(clientPtr, sourceIP, cb);

				return;
			}

			Instance<HttpClient>::Get()->DoPostRequest("https://cfx.re/api/validateSource/?v=1", { { "ip", clep } }, [this, clep, sourceIP, clientPtr, cb](bool success, const char* data, size_t size)
			{
				bool allowSourceIP = true;

				if (success)
				{
					std::string result{ data, size };

					if (result != "yes")
					{
						allowSourceIP = false;
					}
					else
					{
						allowedByPolicyCache.insert({ clep, true });
					}
				}

				if (allowSourceIP)
				{
					SetClientTcpAddress(clientPtr, sourceIP, cb);
				}
				else
				{
					const auto& currentTcpEndPoint = clientPtr->GetTcpEndPoint();
					SetClientIdentifier(clientPtr, currentTcpEndPoint, cb);
				}
			});
		}

	private:
		void SetClientIdentifier(const fx::ClientSharedPtr& client, const std::string& tcpAddress, const std::function<void(boost::optional<std::string>)>& cb)
		{
			client->AddIdentifier("ip:" + tcpAddress);

			cb({});
		}

		void SetClientTcpAddress(const fx::ClientSharedPtr& client, const std::string& tcpEndPoint, const std::function<void(boost::optional<std::string>)>& cb)
		{
			auto tcpAdress = tcpEndPoint.substr(0, tcpEndPoint.find_last_of(':'));

			assert(m_registry != nullptr);
			m_registry->SetClientTcpEndPoint(client, tcpAdress);

			SetClientIdentifier(client, tcpAdress, cb);
		}
	} idp;

	fx::RegisterServerIdentityProvider(&idp);

	fx::ServerInstanceBase::OnServerCreate.Connect([](fx::ServerInstanceBase* instance)
	{
		idp.m_registry = instance->GetComponent<fx::ClientRegistry>().GetRef();
	});
}, 150);
