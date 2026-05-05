#include "TextCapture.h"

#include <algorithm>
#include <utility>

namespace
{
	std::vector<TextCaptureEntry> g_entries;

	bool rectsOverlap(
		INT16 ax, INT16 ay, INT16 aw, INT16 ah,
		INT16 bx, INT16 by, INT16 bw, INT16 bh)
	{
		return ax < bx + bw && ax + aw > bx
		    && ay < by + bh && ay + ah > by;
	}
}

void TextCapture_BeginFrame(void)
{
	// Intentionally a no-op: JA2 paints with dirty rects, so most frames
	// only refresh a small portion of the screen. Clearing here would
	// throw away labels that are still on screen but weren't repainted
	// this frame. The append path is responsible for removing entries
	// whose rects were overdrawn this frame.
}

void TextCapture_Reset(void)
{
	g_entries.clear();
}

void TextCapture_Append(INT16 x, INT16 y, INT16 w, INT16 h, ST::string text)
{
	// No AX_LOG here — every MPrint funnels through this path, and the
	// engine paints hundreds per frame. The snapshot is already queryable
	// via TextCapture_Snapshot() (used by `r list`); a log mirror added
	// 245k lines / 16 MB in an 11-minute session before this cut.

	// Each MPrint call signals "this rect was just repainted on top of
	// whatever was here before". Anything we have stored that overlaps
	// the rect is now stale (overdrawn), so drop it before recording the
	// new entry. Buttons that self-repaint each frame just refresh their
	// own row; labels in untouched regions persist as the user expects.
	const auto stale = std::remove_if(g_entries.begin(), g_entries.end(),
		[&](const TextCaptureEntry& e)
		{
			return rectsOverlap(e.x, e.y, e.w, e.h, x, y, w, h);
		});
	g_entries.erase(stale, g_entries.end());

	g_entries.push_back({ x, y, w, h, std::move(text) });
}

const std::vector<TextCaptureEntry>& TextCapture_Snapshot(void)
{
	return g_entries;
}
