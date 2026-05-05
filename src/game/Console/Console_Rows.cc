#include "Console_Rows.h"

#include "MouseSystem.h"
#include "TextCapture.h"

#include "Console.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <numeric>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	// Two fragments are treated as the same logical row when their Y
	// origins differ by no more than this. Real glyph rows in JA2 use
	// distinct line spacings so a few pixels of slack is plenty without
	// folding adjacent rows together.
	constexpr INT16 ROW_Y_TOLERANCE = 4;

	// Two fragments on the same row are joined when the horizontal gap
	// between the right edge of one and the left edge of the next is at
	// most this many pixels. Save-slot fragments sit ~20–40px apart;
	// the new-game-screen columns are ~270px apart, so 100 separates
	// "fragments of one row" from "two unrelated columns at the same Y".
	constexpr INT16 ROW_X_GAP_MAX = 100;

	struct RowGroup
	{
		std::vector<std::size_t> fragments; // indices into the snapshot
		ST::string               combined;
	};

	std::vector<RowGroup> groupRows(
		const std::vector<TextCaptureEntry>& entries)
	{
		std::vector<std::size_t> order(entries.size());
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(),
			[&](std::size_t a, std::size_t b)
			{
				const auto& ea = entries[a];
				const auto& eb = entries[b];
				if (std::abs(ea.y - eb.y) > ROW_Y_TOLERANCE) return ea.y < eb.y;
				return ea.x < eb.x;
			});

		std::vector<RowGroup> groups;
		for (std::size_t i : order)
		{
			const auto& e = entries[i];
			if (!groups.empty())
			{
				const auto& prev = entries[groups.back().fragments.back()];
				const INT16 yDelta = static_cast<INT16>(std::abs(prev.y - e.y));
				const INT16 xGap   = static_cast<INT16>(e.x - (prev.x + prev.w));
				if (yDelta <= ROW_Y_TOLERANCE && xGap <= ROW_X_GAP_MAX)
				{
					groups.back().fragments.push_back(i);
					groups.back().combined += " | ";
					groups.back().combined += e.text;
					continue;
				}
			}
			groups.push_back({ { i }, e.text });
		}
		return groups;
	}

	bool parseId(const std::string& s, std::size_t& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0' || v < 0) return false;
		out = static_cast<std::size_t>(v);
		return true;
	}

	void cmdList()
	{
		const auto& rows = TextCapture_Snapshot();
		if (rows.empty())
		{
			Console_Println("(no captured text — switch to a screen with text first)");
			return;
		}
		const auto groups = groupRows(rows);
		for (std::size_t i = 0; i < groups.size(); ++i)
		{
			const auto& g = groups[i];
			const auto& first = rows[g.fragments.front()];
			Console_Println(ST::format(
				"  {} — \"{}\" (y={}, {} frag{})",
				i, g.combined, first.y, g.fragments.size(),
				g.fragments.size() == 1 ? "" : "s"));
		}
	}

	void cmdClick(const std::vector<std::string>& args)
	{
		if (args.size() < 3)
		{
			Console_Println("usage: r click <id>");
			return;
		}
		std::size_t id;
		if (!parseId(args[2], id))
		{
			Console_Println(ST::format("invalid row id: {}", args[2]));
			return;
		}
		const auto& rows = TextCapture_Snapshot();
		const auto groups = groupRows(rows);
		if (id >= groups.size())
		{
			Console_Println(ST::format("no row at id {} (have {} rows)", id, groups.size()));
			return;
		}

		// Walk the row's fragments left-to-right and click the first one
		// whose centroid sits inside an enabled mouse region. If a row
		// spans multiple regions or has padded gaps, this gracefully
		// finds the actionable target instead of guessing the centroid
		// of the whole-row bounding box.
		const auto& g = groups[id];
		for (std::size_t fragIdx : g.fragments)
		{
			const auto& f = rows[fragIdx];
			const INT16 cx = static_cast<INT16>(f.x + f.w / 2);
			const INT16 cy = static_cast<INT16>(f.y + f.h / 2);
			MOUSE_REGION* const reg = MSYS_FindRegionAt(cx, cy);
			if (!reg || !reg->ButtonCallback) continue;

			reg->MouseXPos    = cx;
			reg->MouseYPos    = cy;
			reg->RelativeXPos = static_cast<INT16>(cx - reg->RegionTopLeftX);
			reg->RelativeYPos = static_cast<INT16>(cy - reg->RegionTopLeftY);

			reg->ButtonCallback(reg, MSYS_CALLBACK_REASON_LBUTTON_DWN);
			reg->ButtonCallback(reg, MSYS_CALLBACK_REASON_LBUTTON_UP);
			Console_Println(ST::format("clicked row {} (\"{}\")", id, g.combined));
			return;
		}
		Console_Println(ST::format(
			"no clickable region under any fragment of row {} (\"{}\")",
			id, g.combined));
	}
}

void Cmd_Rows(const std::vector<std::string>& args)
{
	// Default to 'list' when invoked bare.
	if (args.size() < 2 || args[1] == "list") { cmdList(); return; }
	if (args[1] == "click") { cmdClick(args); return; }
	Console_Println(ST::format("unknown subcommand: r {} (try 'r list')", args[1]));
}
