#include "Console_Stash.h"

#include "ArmsDealerInvInit.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "Item_Types.h"
#include "ItemModel.h"
#include "StrategicMap.h"
#include "World_Items.h"

#include "Console.h"
#include "Console_Address.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	ST::string itemName(UINT16 usItem)
	{
		if (usItem == 0) return ST::string("empty");
		return GCM->getItem(usItem)->getName();
	}

	// One row per distinct usItem. `count` sums OBJECTTYPE.ubNumberOfObjects
	// across every WORLDITEM contributing — that's the player-visible total
	// (a "stack of 4 mags" shows up in the panel as four overlapping icons,
	// not four separate slots). `sampleStatus` is the first non-stackable
	// item's condition, used to drive the sort tiebreak through
	// CompareItemsForSorting so identical-name rows order the same as the
	// mapscreen panel.
	struct StashRow
	{
		UINT16 usItem;
		INT32  count;
		UINT8  sampleStatus;
	};

	std::vector<StashRow> groupByItem(const std::vector<WORLDITEM>& items)
	{
		std::vector<StashRow> out;
		for (const WORLDITEM& wi : items)
		{
			std::size_t i = 0;
			for (; i < out.size(); ++i) if (out[i].usItem == wi.o.usItem) break;
			if (i == out.size())
			{
				out.push_back({wi.o.usItem, 0, static_cast<UINT8>(wi.o.bStatus[0])});
			}
			out[i].count += wi.o.ubNumberOfObjects;
		}
		std::sort(out.begin(), out.end(),
			[](const StashRow& a, const StashRow& b)
			{
				return CompareItemsForSorting(a.usItem, b.usItem,
					a.sampleStatus, b.sampleStatus) < 0;
			});
		return out;
	}

	void printRows(const SGPSector& sector,
	               const std::vector<WORLDITEM>& items,
	               const char* heading)
	{
		const ST::string label = GetSectorIDString(sector, TRUE);

		if (items.empty())
		{
			Console_Println(ST::format("{} {} — none.", heading, label));
			return;
		}

		const std::vector<StashRow> rows = groupByItem(items);
		INT32 totalCount = 0;
		for (const StashRow& r : rows) totalCount += r.count;

		Console_Println(ST::format("{} {} — {} item{} in {} stack{}:",
			heading, label,
			totalCount, totalCount == 1 ? "" : "s",
			rows.size(), rows.size() == 1 ? "" : "s"));

		for (const StashRow& r : rows)
		{
			if (r.count > 1)
				Console_Println(ST::format("  {} ×{}", itemName(r.usItem), r.count));
			else
				Console_Println(ST::format("  {}", itemName(r.usItem)));
		}
	}
}

void Cmd_Stash(const std::vector<std::string>& args)
{
	SGPSector sector       = gWorldSector;
	bool      hidden       = false;
	bool      sectorParsed = false;

	// Argument shape: stash [<sector>] [hidden]. Either token can come
	// first, but only one sector argument is accepted.
	for (std::size_t i = 1; i < args.size(); ++i)
	{
		const std::string& arg = args[i];
		if (arg == "hidden") { hidden = true; continue; }

		if (!sectorParsed)
		{
			ST::string err;
			SGPSector  probe;
			if (resolveSectorArg(arg, probe, err))
			{
				sector       = probe;
				sectorParsed = true;
				continue;
			}
			Console_Println(ST::format("stash: {}", err));
			return;
		}

		Console_Println(ST::format("stash: unknown argument '{}'", arg.c_str()));
		return;
	}

	const SectorStash stash = LoadSectorStash(sector);
	if (hidden)
	{
		printRows(sector, stash.unseen, "Hidden in");
	}
	else
	{
		printRows(sector, stash.seen, "Stash in");
	}
}
