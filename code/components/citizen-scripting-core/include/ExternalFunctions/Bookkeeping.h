#pragma once

#include <functional>

#include "AsyncResultId.h"
#include "Binding.h"

#include "fxScripting.h"

namespace ExternalFunctions
{
	struct AsyncAwaiter;

	template<bool>
	class ExternalManager;

	namespace Bookkeeping
	{
		enum class Sources
		{
			RESOURCE_CALLER = 0,
			RESOURCE_CALLEE,
			REMOTE,

			COUNT
		};

		struct Node
		{
			template<bool> friend class ExternalFunctions::ExternalManager;

		private:
			struct Links
			{
			public:
				Node** prevNext = nullptr;
				Node* next = nullptr;
			} links[(size_t)Sources::COUNT];

			std::unordered_map<AsyncResultId, AsyncAwaiter>& container;
			AsyncResultId identifier;

		private:
			inline void AddToList(Sources source, Node** prevNext)
			{
				auto& link = links[(size_t)source];
				link.next = *prevNext;
				*prevNext = this;
				link.prevNext = prevNext;
			}

		public:
			Node(std::unordered_map<AsyncResultId, AsyncAwaiter>& container, AsyncResultId identifier)
				: container(container)
				, identifier(identifier)
				, links()
			{ }

			Node(Node&& move) noexcept
				: container(move.container)
				, identifier(move.identifier)
			{
				for (size_t i = 0; i < (size_t)Sources::COUNT; ++i)
					links[i] = std::move(move.links[i]);
			}

			~Node()
			{
				for (size_t i = 0; i < (size_t)Sources::COUNT; ++i)
				{
					auto& link = links[i];

					//trace("\tthread %d\n", std::hash<std::thread::id>{}(std::this_thread::get_id()));

					auto prevNext = link.prevNext;
					if (prevNext)
					{
						//trace("\tprevNext %p\n", (void*)prevNext);

						auto next = *prevNext = link.next;
						if (next)
							next->links[i].prevNext = prevNext;
					}
				}
			}

			inline void Erase()
			{
				container.erase(identifier);
			}

			inline AsyncResultId GetAsyncResultId() const
			{
				return identifier;
			}
		};
	}
}
