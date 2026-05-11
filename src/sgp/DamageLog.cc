#include "DamageLog.h"

namespace DamageLog
{

namespace
{
	constexpr UINT32 kCapacity = 32;
	Record g_buf[kCapacity];
	UINT32 g_total = 0; // monotonic; modulo kCapacity is the slot
}

void Push(const Record& rec)
{
	g_buf[g_total % kCapacity] = rec;
	++g_total;
}

UINT32 Size(void)
{
	return g_total < kCapacity ? g_total : kCapacity;
}

const Record* Get(UINT32 newest_index)
{
	const UINT32 size = Size();
	if (newest_index >= size) return nullptr;
	// Newest lives at (g_total - 1) mod kCapacity; walk backwards.
	const UINT32 raw = (g_total - 1 - newest_index) % kCapacity;
	return &g_buf[raw];
}

const Record* PeekNewest(void)
{
	if (g_total == 0) return nullptr;
	return &g_buf[(g_total - 1) % kCapacity];
}

void Reset(void)
{
	g_total = 0;
}

}
