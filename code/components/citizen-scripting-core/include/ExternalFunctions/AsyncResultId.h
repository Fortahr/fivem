#pragma once

#include <stdint.h>

namespace ExternalFunctions
{
	using PublicId = size_t;
	using PrivateId = size_t;

	union AsyncResultId
	{
		static constexpr uint16_t SERVER = 0xFFFF;

		struct
		{
			uint32_t timestamp; // milliseconds, 1.6 months before reuse
			uint16_t sequence; // up to 65535 calls per millisecond
			uint16_t remote; // up to 65534 player support, 65535 = server
		};

		uint64_t comparer;

		AsyncResultId()
			: comparer(0)
		{ }

		AsyncResultId(uint64_t fullId) noexcept
			: comparer(fullId)
		{ }

		AsyncResultId(const AsyncResultId& copy, uint16_t remote) noexcept
			: remote(remote)
			, sequence(copy.sequence)
			, timestamp(copy.timestamp)
		{ }

		AsyncResultId(uint16_t remote, uint16_t sequence, uint32_t timestamp) noexcept
			: remote(remote)
			, sequence(sequence)
			, timestamp(timestamp)
		{ }

		inline AsyncResultId& operator=(uint64_t other) noexcept
		{
			this->comparer = other;
			return *this;
		}

		inline operator uint64_t() const noexcept
		{
			return this->comparer;
		}

		inline bool operator==(const AsyncResultId& other) const noexcept
		{
			return this->comparer == other.comparer;
		}
	};
}

namespace std
{
	template<>
	struct hash<ExternalFunctions::AsyncResultId>
	{
		size_t operator()(const ExternalFunctions::AsyncResultId& x) const
		{
			return x.comparer;
		}
	};
}
