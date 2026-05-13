#include "Console_Stash.h"

#include "ArmsDealerInvInit.h"
#include "Auto_Resolve.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "Handle_Items.h"
#include "Isometric_Utils.h"
#include "Item_Types.h"
#include "Items.h"
#include "ItemModel.h"
#include "Map_Information.h"
#include "Map_Screen_Interface_Map_Inventory.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Macros.h"
#include "StrategicMap.h"
#include "WorldDef.h"
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

	// One row per (usItem, sGridNo, ubLevel) — a pile in the world. `count`
	// sums OBJECTTYPE.ubNumberOfObjects across every WORLDITEM that shares
	// the same item-at-the-same-tile (rare: usually one WORLDITEM per pile,
	// but a stack of four mags reads ubNumberOfObjects=4 on one record).
	// `sampleStatus` is the first non-stackable item's condition, used for
	// the engine's CompareItemsForSorting tiebreak so identical-name rows
	// order the same as the mapscreen panel.
	struct StashRow
	{
		UINT16 usItem;
		INT16  sGridNo;
		UINT8  ubLevel;
		INT32  count;
		UINT8  sampleStatus;
	};

	std::vector<StashRow> groupByItemAndTile(const std::vector<WORLDITEM>& items)
	{
		std::vector<StashRow> out;
		for (const WORLDITEM& wi : items)
		{
			std::size_t i = 0;
			for (; i < out.size(); ++i)
			{
				if (out[i].usItem == wi.o.usItem &&
				    out[i].sGridNo == wi.sGridNo &&
				    out[i].ubLevel == wi.ubLevel)
					break;
			}
			if (i == out.size())
			{
				out.push_back({wi.o.usItem, wi.sGridNo, wi.ubLevel,
				               0, static_cast<UINT8>(wi.o.bStatus[0])});
			}
			out[i].count += wi.o.ubNumberOfObjects;
		}
		std::sort(out.begin(), out.end(),
			[](const StashRow& a, const StashRow& b)
			{
				const INT32 byItem = CompareItemsForSorting(
					a.usItem, b.usItem, a.sampleStatus, b.sampleStatus);
				if (byItem != 0) return byItem < 0;
				// Tiebreak on gridno so two piles of the same item read
				// in a stable order (and you can re-find a row after a
				// take).
				return a.sGridNo < b.sGridNo;
			});
		return out;
	}

	// Anchor for the per-row offset annotation when listing the loaded
	// sector. Prefer the selected merc, fall back to the map's center
	// gridno so output is still meaningful when nobody's selected.
	INT16 listAnchor()
	{
		const SOLDIERTYPE* const s = GetSelectedMan();
		if (s != nullptr && s->sSector == gWorldSector && !s->fBetweenSectors)
		{
			return s->sGridNo;
		}
		return gMapInformation.sCenterGridNo;
	}

	ST::string locationLabel(const StashRow& r, bool loadedSector, INT16 anchor)
	{
		if (r.sGridNo <= 0) return ST::string("@ ?");
		if (loadedSector)
		{
			ST::string off = formatOffset(anchor, r.sGridNo);
			if (r.ubLevel != 0) off += " (roof)";
			return ST::format("@ {}", off);
		}
		// Remote sector — no world loaded, so a relative offset has no
		// anchor the player can act on. Print raw col,row; the address
		// parser accepts the same form once they enter the sector.
		const INT16 col = r.sGridNo % WORLD_COLS;
		const INT16 row = r.sGridNo / WORLD_COLS;
		if (r.ubLevel != 0) return ST::format("@ {},{} (roof)", col, row);
		return ST::format("@ {},{}", col, row);
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

		const std::vector<StashRow> rows = groupByItemAndTile(items);
		INT32 totalCount = 0;
		for (const StashRow& r : rows) totalCount += r.count;

		const bool  loadedSector = (sector == gWorldSector);
		const INT16 anchor       = loadedSector ? listAnchor() : 0;

		Console_Println(ST::format("{} {} — {} item{} in {} pile{}:",
			heading, label,
			totalCount, totalCount == 1 ? "" : "s",
			rows.size(), rows.size() == 1 ? "" : "s"));

		for (const StashRow& r : rows)
		{
			const ST::string where = locationLabel(r, loadedSector, anchor);
			if (r.count > 1)
				Console_Println(ST::format("  {} ×{} {}",
					itemName(r.usItem), r.count, where));
			else
				Console_Println(ST::format("  {} {}",
					itemName(r.usItem), where));
		}
	}

	// Predicate the GUI panel uses to decide what's in the "Sector
	// Inventory" panel vs the hidden pool. Re-implemented here for
	// directness — same record, same conditions — so the verbs operate on
	// the live gWorldItems without going through the panel's local copy.
	bool worldItemMatchesVisibility(const WORLDITEM& wi, bool wantHidden)
	{
		if (!wi.fExists) return false;
		const bool seen = IsMapScreenWorldItemVisibleInMapInventory(wi);
		return wantHidden ? !seen : seen;
	}

	// Refuse the take when the engine's mapscreen GUI would also refuse:
	// in active combat the panel forces you to do transfers in tactical.
	// Mirrors CanPlayerUseSectorInventory() at
	// Map_Screen_Interface_Map_Inventory.cc:1315 except we gate on the
	// merc's sector instead of sSelMap (which is the mapscreen cursor,
	// not necessarily where the merc stands).
	bool sectorInCombat(const SGPSector& sector)
	{
		SGPSector battle;
		if (!GetCurrentBattleSectorXYZAndReturnTRUEIfThereIsABattle(battle))
			return false;
		return battle == sector;
	}

	void doStashTake(const std::vector<std::string>& args)
	{
		if (args.size() < 3)
		{
			Console_Println(
				"usage: stash take <pile> [merc]  (pile: i<N> from "
				"'nearby items', or compass+steps, or col,row)");
			return;
		}

		// Resolve the recipient first — parseTarget needs an observer to
		// anchor `i<N>` and compass directions on, and the natural
		// observer is the merc receiving the items. Allow an explicit
		// [merc] override after the address; default = selected.
		SOLDIERTYPE* taker = GetSelectedMan();
		if (!taker)
		{
			Console_Println("No merc selected.");
			return;
		}

		Target     tgt;
		ST::string err;
		const int consumed = parseTarget(args, 2, taker, tgt, err);
		if (consumed == 0) { Console_Println(err); return; }

		// Anything after the address is the optional recipient name.
		for (std::size_t i = 2 + consumed; i < args.size(); ++i)
		{
			ST::string nameErr;
			SOLDIERTYPE* const named = findTeammateByName(args[i], nameErr);
			if (!named) { Console_Println(nameErr); return; }
			taker = named;
		}

		// The take operates on gWorldItems, so the merc must be in the
		// loaded sector. Mirrors the GUI gate at
		// Map_Screen_Interface_Map_Inventory.cc:539-547.
		if (taker->sSector != gWorldSector || taker->fBetweenSectors)
		{
			Console_Println(ST::format(
				"{} is not in the loaded sector.", taker->name));
			return;
		}
		if (AM_AN_EPC(taker))
		{
			Console_Println(ST::format(
				"{} is an escort and refuses to handle items.",
				taker->name));
			return;
		}
		if (sectorInCombat(taker->sSector))
		{
			Console_Println(
				"Sector is in active combat. Pick items up in tactical "
				"instead (try 'pickup <target>').");
			return;
		}
		// While the sector-inventory panel is up its pInventoryPoolList
		// is the live edit buffer and writes back to gWorldItems only on
		// close; touching gWorldItems now would be clobbered. Close it
		// first.
		if (fShowMapInventoryPool)
		{
			Console_Println(
				"Close the Sector Inventory panel first.");
			return;
		}

		const INT16 gridno = tgt.gridno;
		// Tile-granular semantics: take every visible, reachable
		// WORLDITEM at this (gridno, level). Matches `pickup`'s "grab
		// what's here" model. Mercs sit at ubLevel == 0 for ground and
		// 1 for rooftops; the take uses the merc's level so addressing
		// a rooftop tile from the ground (or vice versa) lifts only the
		// items the player would naturally reach.
		const UINT8 wantLevel = taker->bLevel;
		std::vector<INT32> indices;
		for (std::size_t i = 0; i < gWorldItems.size(); ++i)
		{
			const WORLDITEM& wi = gWorldItems[i];
			if (!worldItemMatchesVisibility(wi, false)) continue;
			if (wi.sGridNo != gridno)                   continue;
			if (wi.ubLevel != wantLevel)                continue;
			if (!(wi.usFlags & WORLD_ITEM_REACHABLE))   continue;
			indices.push_back(static_cast<INT32>(i));
		}

		if (indices.empty())
		{
			Console_Println(ST::format(
				"No items to take at {}.",
				formatOffset(taker->sGridNo, gridno)));
			return;
		}

		// Take each WORLDITEM in turn. AutoPlaceObject mutates pObj —
		// if anything couldn't fit, push the remainder back at the
		// original tile so the player isn't punished with vanished
		// items when pockets are full. Collect per-item results for the
		// summary line.
		struct Took
		{
			ST::string name;
			INT32      moved;
			INT32      leftAtTile;
		};
		std::vector<Took> log;
		bool anyOverflow = false;

		for (INT32 idx : indices)
		{
			WORLDITEM&       wi      = gWorldItems[idx];
			const UINT16     usItem  = wi.o.usItem;
			const INT32      srcN    = wi.o.ubNumberOfObjects;
			const ST::string name    = GCM->getItem(usItem)->getName();

			OBJECTTYPE temp = wi.o;
			RemoveItemFromPool(wi);
			AutoPlaceObject(taker, &temp, TRUE);
			const INT32 leftover = temp.ubNumberOfObjects;
			const INT32 moved    = srcN - leftover;

			if (leftover > 0)
			{
				AddItemToPool(gridno, &temp, VISIBLE, wantLevel, 0, -1);
				anyOverflow = true;
			}
			log.push_back({name, moved, leftover});
		}

		const ST::string where = formatOffset(taker->sGridNo, gridno);

		// One-line summary for the common single-item case; itemized
		// breakdown when several items are involved.
		if (log.size() == 1)
		{
			const Took& t = log.front();
			if (t.moved == 0)
			{
				Console_Println(ST::format(
					"{}: no room for {} (left at {}).",
					taker->name, t.name, where));
			}
			else if (t.leftAtTile > 0)
			{
				Console_Println(ST::format(
					"{} took {} ×{} from {}; {} left at the tile "
					"(inventory full).",
					taker->name, t.name, t.moved, where, t.leftAtTile));
			}
			else if (t.moved == 1)
			{
				Console_Println(ST::format(
					"{} took {} from {}.", taker->name, t.name, where));
			}
			else
			{
				Console_Println(ST::format(
					"{} took {} ×{} from {}.",
					taker->name, t.name, t.moved, where));
			}
			return;
		}

		Console_Println(ST::format("{} took from {}:", taker->name, where));
		for (const Took& t : log)
		{
			if (t.moved == 0)
				Console_Println(ST::format("  {}: no room (left at tile).",
					t.name));
			else if (t.leftAtTile > 0)
				Console_Println(ST::format("  {} ×{} (×{} left at tile)",
					t.name, t.moved, t.leftAtTile));
			else if (t.moved == 1)
				Console_Println(ST::format("  {}", t.name));
			else
				Console_Println(ST::format("  {} ×{}", t.name, t.moved));
		}
		if (anyOverflow)
		{
			Console_Println(
				"  (inventory full — leftover items are back at the tile)");
		}
	}
}

void Cmd_Stash(const std::vector<std::string>& args)
{
	// Subcommand: stash take <item> [merc] [hidden]. The "take" keyword
	// is a fixed prefix — no JA2 sector name or item alias is "take",
	// so this routing is unambiguous.
	if (args.size() >= 2 && args[1] == "take")
	{
		doStashTake(args);
		return;
	}

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
