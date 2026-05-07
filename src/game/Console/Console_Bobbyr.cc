#include "Console_Bobbyr.h"

#include "ArmourModel.h"
#include "ArmsDealerInvInit.h"
#include "BobbyR.h"
#include "BobbyRGuns.h"
#include "BobbyRMailOrder.h"
#include "CalibreModel.h"
#include "ContentManager.h"
#include "Finances.h"
#include "Game_Clock.h"
#include "GameInstance.h"
#include "Item_Types.h"
#include "ItemModel.h"
#include "JAScreens.h"
#include "Laptop.h"
#include "LaptopSave.h"
#include "MagazineModel.h"
#include "ScreenIDs.h"
#include "ShippingDestinationModel.h"
#include "Store_Inventory.h"
#include "Types.h"
#include "WeaponModels.h"
#include "WordWrap.h"

#include "Console.h"
#include "Console_Address.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	// ----- General helpers --------------------------------------------

	std::string joinFrom(const std::vector<std::string>& args, std::size_t from)
	{
		std::string out;
		for (std::size_t i = from; i < args.size(); ++i)
		{
			if (i > from) out += " ";
			out += args[i];
		}
		return lower(out);
	}

	// ----- Gates ------------------------------------------------------

	bool unlockGate()
	{
		if (LaptopSaveInfo.fBobbyRSiteCanBeAccessed) return true;
		Console_Println(
			"Bobby Ray's is still under construction — liberate Drassen's "
			"airport (or use 'cheat bobbyr' for dev) to unlock the site.");
		return false;
	}

	bool laptopGate()
	{
		if (guiCurrentScreen == LAPTOP_SCREEN) return true;
		Console_Println("Open your laptop first.");
		return false;
	}

	// ----- Category model ---------------------------------------------

	enum Category { CAT_GUNS, CAT_AMMO, CAT_ARMOR, CAT_MISC, CAT_USED };

	const char* categoryName(Category c)
	{
		switch (c)
		{
			case CAT_GUNS:  return "guns";
			case CAT_AMMO:  return "ammo";
			case CAT_ARMOR: return "armor";
			case CAT_MISC:  return "misc";
			case CAT_USED:  return "used";
		}
		return "?";
	}

	bool parseCategory(const std::string& s, Category& out)
	{
		const std::string l = lower(s);
		if (l == "guns" || l == "gun")             { out = CAT_GUNS;  return true; }
		if (l == "ammo")                            { out = CAT_AMMO;  return true; }
		if (l == "armor" || l == "armour")          { out = CAT_ARMOR; return true; }
		if (l == "misc" || l == "miscellaneous")    { out = CAT_MISC;  return true; }
		if (l == "used")                            { out = CAT_USED;  return true; }
		return false;
	}

	// Item class predicate matching the GUI's five top-level pages. The
	// engine's sort table (DealerItemSortInfo) groups slightly more
	// finely than the user-visible categories — these masks fold each
	// page back to its single check.
	bool itemMatchesCategory(const ItemModel* item, Category cat)
	{
		const uint32_t cls = item->getItemClass();
		switch (cat)
		{
			case CAT_GUNS:  return (cls & IC_BOBBY_GUN) != 0;
			case CAT_AMMO:  return (cls & IC_AMMO) != 0;
			case CAT_ARMOR: return (cls & IC_ARMOUR) != 0;
			case CAT_MISC:  return (cls & IC_BOBBY_MISC) != 0;
			case CAT_USED:  return true; // already filtered by inventory list
		}
		return false;
	}

	// ----- bN tag table -----------------------------------------------
	// Populated by `bobbyr list`, consumed by `bobbyr show / add /
	// remove`. Stable within one invocation; the next list re-tags.

	struct ListTag
	{
		UINT16 bobbyIdx;  // slot in BobbyRayInventory or BobbyRayUsedInventory
		bool   fUsed;     // which inventory bobbyIdx indexes
	};
	std::vector<ListTag> g_lastTags;

	// ----- Filter / sort ----------------------------------------------

	enum FilterMode { FILT_AVAILABLE, FILT_ALL, FILT_ONORDER };

	bool parseFilter(const std::string& s, FilterMode& out)
	{
		const std::string l = lower(s);
		if (l == "available" || l == "instock") { out = FILT_AVAILABLE; return true; }
		if (l == "all")                          { out = FILT_ALL;       return true; }
		if (l == "onorder")                      { out = FILT_ONORDER;   return true; }
		return false;
	}

	bool filterAccepts(FilterMode mode, const STORE_INVENTORY& inv)
	{
		switch (mode)
		{
			case FILT_AVAILABLE: return inv.ubQtyOnHand > 0;
			case FILT_ALL:       return true;
			case FILT_ONORDER:   return inv.ubQtyOnOrder > 0;
		}
		return false;
	}

	enum SortKey { SORT_PRICE, SORT_NAME, SORT_STOCK };

	bool parseSortKey(const std::string& s, SortKey& out)
	{
		const std::string l = lower(s);
		if (l == "price") { out = SORT_PRICE; return true; }
		if (l == "name")  { out = SORT_NAME;  return true; }
		if (l == "stock") { out = SORT_STOCK; return true; }
		return false;
	}

	const char* sortKeyName(SortKey k)
	{
		switch (k)
		{
			case SORT_PRICE: return "price";
			case SORT_NAME:  return "name";
			case SORT_STOCK: return "stock";
		}
		return "?";
	}

	// ----- Inventory access -------------------------------------------

	STORE_INVENTORY* invArray(bool fUsed)
	{
		return fUsed ? LaptopSaveInfo.BobbyRayUsedInventory
		             : LaptopSaveInfo.BobbyRayInventory;
	}

	UINT16 invLength(bool fUsed)
	{
		return LaptopSaveInfo.usInventoryListLength[
			fUsed ? BOBBY_RAY_USED : BOBBY_RAY_NEW];
	}

	// ----- Item resolution --------------------------------------------
	// Resolves a user-typed name|bN to (bobbyIdx, fUsed). Returns true on
	// success. On miss, populates `err` with a hint.
	//
	// Match order:
	//   bN         -> g_lastTags[N-1]
	//   exact name -> the single item whose Bobby-Ray name matches
	//                 (case-insensitive). New inventory wins over used.
	//   prefix name -> single matching prefix across new+used.
	bool resolveItem(const std::string& wantRaw, UINT16& outBobbyIdx,
		bool& outUsed, ST::string& err)
	{
		const std::string want = lower(wantRaw);
		if (want.empty())
		{
			err = ST::string("missing item (try 'bobbyr list <cat>' for tags)");
			return false;
		}

		// bN
		if (want[0] == 'b' && want.size() >= 2)
		{
			long n;
			if (parseInt(want.substr(1), n))
			{
				if (n < 1 || static_cast<std::size_t>(n) > g_lastTags.size())
				{
					err = ST::format(
						"tag {} out of range (run 'bobbyr list' first; "
						"1..{} valid)",
						wantRaw, g_lastTags.size());
					return false;
				}
				const auto& t = g_lastTags[n - 1];
				outBobbyIdx = t.bobbyIdx;
				outUsed     = t.fUsed;
				return true;
			}
		}

		// Name search across new then used.
		auto match = [&](bool fUsed, INT32& exact, INT32& prefix,
			std::size_t& prefixHits) -> void
		{
			STORE_INVENTORY* inv = invArray(fUsed);
			const UINT16 n = invLength(fUsed);
			for (UINT16 i = 0; i < n; ++i)
			{
				if (inv[i].usItemIndex == 0) continue;
				const ItemModel* item = GCM->getItem(inv[i].usItemIndex);
				if (!item) continue;
				const std::string nm = lower(item->getBobbyRaysName().to_std_string());
				if (nm.empty()) continue;
				if (nm == want) { exact = i; }
				else if (nm.rfind(want, 0) == 0) { prefix = i; ++prefixHits; }
			}
		};

		INT32 exactNew = -1, prefixNew = -1;
		std::size_t prefNewHits = 0;
		match(false, exactNew, prefixNew, prefNewHits);
		if (exactNew >= 0)
		{
			outBobbyIdx = static_cast<UINT16>(exactNew);
			outUsed     = false;
			return true;
		}

		INT32 exactUsed = -1, prefixUsed = -1;
		std::size_t prefUsedHits = 0;
		match(true, exactUsed, prefixUsed, prefUsedHits);
		if (exactUsed >= 0)
		{
			outBobbyIdx = static_cast<UINT16>(exactUsed);
			outUsed     = true;
			return true;
		}

		// Prefer unique prefix in new; only fall back to used if new had none.
		if (prefNewHits == 1)
		{
			outBobbyIdx = static_cast<UINT16>(prefixNew);
			outUsed     = false;
			return true;
		}
		if (prefNewHits == 0 && prefUsedHits == 1)
		{
			outBobbyIdx = static_cast<UINT16>(prefixUsed);
			outUsed     = true;
			return true;
		}

		if (prefNewHits + prefUsedHits > 1)
		{
			err = ST::format(
				"ambiguous item: {} (try 'bobbyr list <cat>' to list)",
				wantRaw);
		}
		else
		{
			err = ST::format(
				"no Bobby Ray's item named: {} (try 'bobbyr list <cat>')",
				wantRaw);
		}
		return false;
	}

	// ----- Cart access -------------------------------------------------

	INT8 findCartLine(UINT16 bobbyIdx, bool fUsed)
	{
		for (UINT8 i = 0; i < MAX_PURCHASE_AMOUNT; ++i)
		{
			const auto& p = BobbyRayPurchases[i];
			if (p.ubNumberPurchased == 0) continue;
			if (p.usBobbyItemIndex == bobbyIdx && (p.fUsed != FALSE) == fUsed)
				return i;
		}
		return -1;
	}

	INT8 findFreeCartSlot()
	{
		for (UINT8 i = 0; i < MAX_PURCHASE_AMOUNT; ++i)
		{
			if (BobbyRayPurchases[i].ubNumberPurchased == 0) return i;
		}
		return -1;
	}

	UINT8 cartUsedSlots()
	{
		UINT8 n = 0;
		for (UINT8 i = 0; i < MAX_PURCHASE_AMOUNT; ++i)
		{
			if (BobbyRayPurchases[i].ubNumberPurchased != 0) ++n;
		}
		return n;
	}

	// ----- Per-class spec output --------------------------------------

	void printGunSpecs(const ItemModel* item)
	{
		const WeaponModel* w = item->asWeapon();
		if (!w) return;
		ST::string line = ST::format("  Caliber {}.",
			w->calibre ? w->calibre->getName() : ST::string("?"));
		Console_Println(line);
		Console_Println(ST::format(
			"  Magazine {} rounds. Range {}. Impact {}. Rate of fire {}.",
			w->ubMagSize, w->usRange, w->ubImpact, w->getRateOfFire()));
	}

	void printAmmoSpecs(const ItemModel* item)
	{
		const MagazineModel* m = item->asAmmo();
		if (!m) return;
		Console_Println(ST::format(
			"  Caliber {}. Capacity {} rounds.",
			m->calibre ? m->calibre->getName() : ST::string("?"),
			m->capacity));
	}

	void printArmorSpecs(const ItemModel* item)
	{
		const ArmourModel* a = item->asArmour();
		if (!a) return;
		Console_Println(ST::format(
			"  Protection {}. Class {}. Explosives protection {}.",
			a->getProtection(), a->getArmourClass(),
			a->getExplosivesProtection()));
	}

	// ----- Status word for shipping speed -----------------------------

	const char* speedName(UINT8 speed)
	{
		switch (speed)
		{
			case 0: return "overnight";
			case 1: return "2-day";
			case 2: return "standard";
		}
		return "(none)";
	}

	UINT8 speedToDaysAhead(UINT8 speed)
	{
		switch (speed)
		{
			case 0: return 1; // OVERNIGHT_EXPRESS
			case 1: return 2; // TWO_BUSINESS_DAYS
			case 2: return 3; // STANDARD_SERVICE
		}
		return 0;
	}

	// ----- bobbyr (summary) -------------------------------------------

	void cmdSummary(const std::vector<std::string>&)
	{
		if (!unlockGate()) return;

		std::array<UINT32, 5> stockCounts{};   // by Category enum
		std::array<UINT32, 5> onOrderCounts{};

		auto walk = [&](bool fUsed)
		{
			STORE_INVENTORY* inv = invArray(fUsed);
			const UINT16 n = invLength(fUsed);
			for (UINT16 i = 0; i < n; ++i)
			{
				const ItemModel* item = GCM->getItem(inv[i].usItemIndex);
				if (!item) continue;
				if (fUsed)
				{
					stockCounts[CAT_USED]   += inv[i].ubQtyOnHand;
					onOrderCounts[CAT_USED] += inv[i].ubQtyOnOrder;
					continue;
				}
				for (Category c : { CAT_GUNS, CAT_AMMO, CAT_ARMOR, CAT_MISC })
				{
					if (itemMatchesCategory(item, c))
					{
						stockCounts[c]   += inv[i].ubQtyOnHand;
						onOrderCounts[c] += inv[i].ubQtyOnOrder;
						break;
					}
				}
			}
		};
		walk(false);
		walk(true);

		const UINT8 cartLines    = cartUsedSlots();
		const UINT16 inTransit   = CountNumberOfBobbyPurchasesThatAreInTransit();

		ST::string cartSummary = cartLines == 0
			? ST::string("empty")
			: ST::format("{} line{}, {} subtotal", cartLines,
				cartLines == 1 ? "" : "s",
				SPrintMoney(static_cast<INT32>(
					BobbyR_GetOrderState().subtotal)));
		Console_Println(ST::format(
			"Bobby Ray's: {} cash. Cart: {}. {} pending shipment{}.",
			SPrintMoney(LaptopSaveInfo.iCurrentBalance),
			cartSummary,
			inTransit, inTransit == 1 ? "" : "s"));

		Console_Println(ST::format(
			"  Guns:  {} in stock, {} on order.",
			stockCounts[CAT_GUNS], onOrderCounts[CAT_GUNS]));
		Console_Println(ST::format(
			"  Ammo:  {} in stock, {} on order.",
			stockCounts[CAT_AMMO], onOrderCounts[CAT_AMMO]));
		Console_Println(ST::format(
			"  Armor: {} in stock, {} on order.",
			stockCounts[CAT_ARMOR], onOrderCounts[CAT_ARMOR]));
		Console_Println(ST::format(
			"  Misc:  {} in stock, {} on order.",
			stockCounts[CAT_MISC], onOrderCounts[CAT_MISC]));
		Console_Println(ST::format(
			"  Used:  {} in stock, {} on order.",
			stockCounts[CAT_USED], onOrderCounts[CAT_USED]));

		Console_Println(
			"Use 'bobbyr list <cat>' to browse, 'bobbyr cart' for the cart.");
	}

	// ----- bobbyr list ------------------------------------------------

	void cmdList(const std::vector<std::string>& args)
	{
		if (!unlockGate()) return;
		if (args.size() < 3)
		{
			Console_Println(
				"usage: bobbyr list <guns|ammo|armor|misc|used> "
				"[available|all|onorder] [price|name|stock] [asc|desc]");
			return;
		}

		Category cat;
		if (!parseCategory(args[2], cat))
		{
			Console_Println(ST::format(
				"unknown category: {} (try guns, ammo, armor, misc, used)",
				args[2]));
			return;
		}

		FilterMode filter = FILT_AVAILABLE;
		SortKey    sort   = SORT_PRICE;
		bool       ascend = true;

		std::size_t at = 3;
		if (at < args.size())
		{
			FilterMode f;
			if (parseFilter(args[at], f)) { filter = f; ++at; }
		}
		if (at < args.size())
		{
			SortKey k;
			if (parseSortKey(args[at], k)) { sort = k; ++at; }
		}
		if (at < args.size())
		{
			const std::string d = lower(args[at]);
			if      (d == "asc")  { ascend = true;  ++at; }
			else if (d == "desc") { ascend = false; ++at; }
		}

		const bool fUsed = (cat == CAT_USED);
		STORE_INVENTORY* inv = invArray(fUsed);
		const UINT16     n   = invLength(fUsed);

		struct Row { UINT16 idx; const ItemModel* item; UINT16 unitPrice; };
		std::vector<Row> rows;
		rows.reserve(n);

		for (UINT16 i = 0; i < n; ++i)
		{
			if (!filterAccepts(filter, inv[i])) continue;
			const ItemModel* item = GCM->getItem(inv[i].usItemIndex);
			if (!item) continue;
			if (!fUsed && !itemMatchesCategory(item, cat)) continue;
			rows.push_back({ i, item,
				CalcBobbyRayCost(inv[i].usItemIndex, i, fUsed) });
		}

		std::sort(rows.begin(), rows.end(), [&](const Row& l, const Row& r)
		{
			switch (sort)
			{
				case SORT_PRICE:
					return ascend ? l.unitPrice < r.unitPrice
					              : l.unitPrice > r.unitPrice;
				case SORT_NAME:
				{
					const auto& la = l.item->getBobbyRaysName();
					const auto& ra = r.item->getBobbyRaysName();
					return ascend ? la.compare(ra) < 0 : la.compare(ra) > 0;
				}
				case SORT_STOCK:
				{
					const UINT8 ls = invArray(fUsed)[l.idx].ubQtyOnHand;
					const UINT8 rs = invArray(fUsed)[r.idx].ubQtyOnHand;
					return ascend ? ls < rs : ls > rs;
				}
			}
			return false;
		});

		g_lastTags.clear();
		g_lastTags.reserve(rows.size());

		Console_Println(ST::format(
			"{} ({}) — filter: {}, sorted by {} {}:",
			categoryName(cat), rows.size(),
			filter == FILT_AVAILABLE ? "available" :
				filter == FILT_ALL ? "all" : "on-order",
			sortKeyName(sort), ascend ? "ascending" : "descending"));

		if (rows.empty()) { Console_Println("(no matches)"); return; }

		std::size_t i = 1;
		for (const auto& row : rows)
		{
			g_lastTags.push_back({ row.idx, fUsed });
			const STORE_INVENTORY& slot = invArray(fUsed)[row.idx];
			ST::string tail;
			if (fUsed)
				tail = ST::format(" (Q{}%)", slot.ubItemQuality);
			else if (slot.ubQtyOnOrder > 0)
				tail = ST::format(" ({} on order)", slot.ubQtyOnOrder);

			Console_Println(ST::format(
				"  b{}  {} — {} — in stock {} — {} lb{}",
				i++,
				row.item->getBobbyRaysName(),
				SPrintMoney(row.unitPrice),
				slot.ubQtyOnHand,
				ST::format("{2.1f}", row.item->getWeight() / 10.0),
				tail));
		}
	}

	// ----- bobbyr show ------------------------------------------------

	void cmdShow(const std::vector<std::string>& args)
	{
		if (!unlockGate()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: bobbyr show <name|bN>");
			return;
		}
		UINT16 idx; bool fUsed; ST::string err;
		if (!resolveItem(joinFrom(args, 2), idx, fUsed, err))
		{
			Console_Println(err); return;
		}
		const STORE_INVENTORY& slot = invArray(fUsed)[idx];
		const ItemModel* item = GCM->getItem(slot.usItemIndex);
		if (!item) { Console_Println("(no item data)"); return; }
		const UINT16 unitPrice = CalcBobbyRayCost(slot.usItemIndex, idx, fUsed);

		Console_Println(ST::format(
			"{}{} — {}. {} lb. In stock {}{}{}.",
			item->getBobbyRaysName(),
			fUsed ? ST::string(" (used)") : ST::string(),
			SPrintMoney(unitPrice),
			ST::format("{2.1f}", item->getWeight() / 10.0),
			slot.ubQtyOnHand,
			slot.ubQtyOnOrder > 0 ?
				ST::format(", {} on order", slot.ubQtyOnOrder) : ST::string(),
			fUsed ? ST::format(", quality {}%", slot.ubItemQuality) :
				ST::string()));

		if (item->isGun() || item->isLauncher()) printGunSpecs(item);
		else if (item->isAmmo())                  printAmmoSpecs(item);
		else if (item->isArmour())                printArmorSpecs(item);

		const ST::string desc =
			CleanOutControlCodesFromString(item->getBobbyRaysDescription());
		if (!desc.empty())
		{
			Console_Println("");
			Console_Println(desc);
		}
	}

	// ----- bobbyr add -------------------------------------------------

	void cmdAdd(const std::vector<std::string>& args)
	{
		if (!unlockGate()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: bobbyr add <name|bN> [qty]");
			return;
		}

		// Last token may be qty if numeric.
		long qty = 1;
		std::size_t nameEnd = args.size();
		if (args.size() > 3 && parseInt(args.back(), qty))
		{
			--nameEnd;
		}
		else
		{
			qty = 1;
		}
		if (qty < 1) { Console_Println("qty must be >= 1"); return; }

		// Reconstruct the name from tokens [2 .. nameEnd).
		std::string name;
		for (std::size_t i = 2; i < nameEnd; ++i)
		{
			if (i > 2) name += " ";
			name += args[i];
		}
		name = lower(name);

		UINT16 idx; bool fUsed; ST::string err;
		if (!resolveItem(name, idx, fUsed, err))
		{
			Console_Println(err); return;
		}

		const STORE_INVENTORY& slot = invArray(fUsed)[idx];
		const UINT8 stock = slot.ubQtyOnHand;
		if (stock == 0)
		{
			Console_Println(ST::format(
				"{} is out of stock.",
				GCM->getItem(slot.usItemIndex)->getBobbyRaysName()));
			return;
		}

		INT8 line = findCartLine(idx, fUsed);
		if (line == -1)
		{
			line = findFreeCartSlot();
			if (line == -1)
			{
				Console_Println(ST::format(
					"cart full ({}/{} lines used). Use 'bobbyr checkout' to "
					"ship the current cart, or 'bobbyr remove <name>' to "
					"free a line.",
					cartUsedSlots(), MAX_PURCHASE_AMOUNT));
				return;
			}
			BobbyRayPurchases[line].usItemIndex      = slot.usItemIndex;
			BobbyRayPurchases[line].usBobbyItemIndex = idx;
			BobbyRayPurchases[line].fUsed            = fUsed ? TRUE : FALSE;
			BobbyRayPurchases[line].bItemQuality     =
				fUsed ? slot.ubItemQuality : 100;
			BobbyRayPurchases[line].ubNumberPurchased = 0;
		}

		auto& p = BobbyRayPurchases[line];
		const UINT8 cap = std::min<UINT8>(stock,
			BOBBY_RAY_MAX_AMOUNT_OF_ITEMS_TO_PURCHASE);
		const UINT8 want = static_cast<UINT8>(
			std::min<long>(qty, 255 - p.ubNumberPurchased));
		const UINT8 added = static_cast<UINT8>(
			std::min<int>(want, cap - p.ubNumberPurchased));
		if (added == 0)
		{
			Console_Println(ST::format(
				"cannot add: {} already at line cap ({}/{}).",
				GCM->getItem(slot.usItemIndex)->getBobbyRaysName(),
				p.ubNumberPurchased, cap));
			return;
		}
		p.ubNumberPurchased = static_cast<UINT8>(p.ubNumberPurchased + added);

		const UINT16 unitPrice = CalcBobbyRayCost(p.usItemIndex,
			p.usBobbyItemIndex, p.fUsed);
		const UINT32 lineTotal = unitPrice * p.ubNumberPurchased;
		const UINT32 cartTotal = BobbyR_GetOrderState().subtotal;

		Console_Println(ST::format(
			"Added {} × {}{} (now {} in cart, {}). Cart subtotal: {}.",
			added,
			GCM->getItem(p.usItemIndex)->getBobbyRaysName(),
			fUsed ? ST::string(" (used)") : ST::string(),
			p.ubNumberPurchased,
			SPrintMoney(static_cast<INT32>(lineTotal)),
			SPrintMoney(static_cast<INT32>(cartTotal))));

		if (added < qty)
		{
			Console_Println(ST::format(
				"  (capped at {} — stock {} or per-line cap {})",
				added, stock, BOBBY_RAY_MAX_AMOUNT_OF_ITEMS_TO_PURCHASE));
		}
	}

	// ----- bobbyr remove ----------------------------------------------

	void cmdRemove(const std::vector<std::string>& args)
	{
		if (!unlockGate()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: bobbyr remove <name|bN> [qty|all]");
			return;
		}

		long qty = 1;
		bool removeAll = false;
		std::size_t nameEnd = args.size();
		if (args.size() > 3)
		{
			const std::string last = lower(args.back());
			if (last == "all") { removeAll = true; --nameEnd; }
			else if (parseInt(last, qty)) { --nameEnd; }
		}
		if (!removeAll && qty < 1)
		{
			Console_Println("qty must be >= 1, or use 'all'");
			return;
		}

		std::string name;
		for (std::size_t i = 2; i < nameEnd; ++i)
		{
			if (i > 2) name += " ";
			name += args[i];
		}
		name = lower(name);

		UINT16 idx; bool fUsed; ST::string err;
		if (!resolveItem(name, idx, fUsed, err))
		{
			Console_Println(err); return;
		}

		const INT8 line = findCartLine(idx, fUsed);
		if (line == -1)
		{
			Console_Println(ST::format(
				"{}{} is not in the cart.",
				GCM->getItem(invArray(fUsed)[idx].usItemIndex)->getBobbyRaysName(),
				fUsed ? ST::string(" (used)") : ST::string()));
			return;
		}

		auto& p = BobbyRayPurchases[line];
		const UINT8 was = p.ubNumberPurchased;
		const UINT8 take = removeAll ? was :
			static_cast<UINT8>(std::min<long>(qty, was));
		p.ubNumberPurchased = static_cast<UINT8>(was - take);
		if (p.ubNumberPurchased == 0)
		{
			p.usBobbyItemIndex = 0;
			p.usItemIndex      = 0;
			p.fUsed            = FALSE;
		}
		Console_Println(ST::format(
			"Removed {} × {}{}. Cart subtotal: {}.",
			take,
			GCM->getItem(invArray(fUsed)[idx].usItemIndex)->getBobbyRaysName(),
			fUsed ? ST::string(" (used)") : ST::string(),
			SPrintMoney(
				static_cast<INT32>(BobbyR_GetOrderState().subtotal))));
	}

	// ----- bobbyr cart ------------------------------------------------

	void cmdCart(const std::vector<std::string>&)
	{
		if (!unlockGate()) return;
		const auto st = BobbyR_GetOrderState();
		if (!st.anyInCart) { Console_Println("Cart is empty."); return; }

		Console_Println("Cart:");
		for (UINT8 i = 0; i < MAX_PURCHASE_AMOUNT; ++i)
		{
			const auto& p = BobbyRayPurchases[i];
			if (p.ubNumberPurchased == 0) continue;
			const UINT16 unitPrice = CalcBobbyRayCost(p.usItemIndex,
				p.usBobbyItemIndex, p.fUsed);
			const UINT32 lineTotal = unitPrice * p.ubNumberPurchased;
			Console_Println(ST::format(
				"  {} × {}{} @ {} = {}",
				p.ubNumberPurchased,
				GCM->getItem(p.usItemIndex)->getBobbyRaysName(),
				p.fUsed ? ST::format(" (used Q{}%)", p.bItemQuality) :
					ST::string(),
				SPrintMoney(unitPrice),
				SPrintMoney(static_cast<INT32>(lineTotal))));
		}
		Console_Println(ST::format(
			"Subtotal {}, weight {} lb.",
			SPrintMoney(static_cast<INT32>(st.subtotal)),
			ST::format("{2.1f}", st.packageWeight / 10.0)));

		if (st.selectedCity != -1)
		{
			auto dest = GCM->getShippingDestination(st.selectedCity);
			Console_Println(ST::format(
				"Ship to: {}. Speed: {} ({} shipping). Grand total: {}.",
				dest->name, speedName(st.selectedSpeed),
				SPrintMoney(static_cast<INT32>(st.shippingCost)),
				SPrintMoney(static_cast<INT32>(st.grandTotal))));
		}
		else
		{
			Console_Println("Ship to: (not set — try 'bobbyr ship <city>').");
		}
	}

	// ----- bobbyr clear -----------------------------------------------

	void cmdClear(const std::vector<std::string>&)
	{
		if (!unlockGate()) return;
		BobbyR_ClearCart();
		Console_Println("Cart cleared.");
	}

	// ----- bobbyr ship ------------------------------------------------

	void cmdShip(const std::vector<std::string>& args)
	{
		if (!unlockGate()) return;
		if (args.size() < 3)
		{
			Console_Println(
				"usage: bobbyr ship <city|sector|default>");
			return;
		}
		const std::string raw = joinFrom(args, 2);

		const auto& dests = GCM->getShippingDestinations();
		const ShippingDestinationModel* match = nullptr;

		if (raw == "default" || raw == "primary")
		{
			match = GCM->getPrimaryShippingDestination();
		}
		else
		{
			// Try as a sector short string ("a9") first.
			SGPSector probe = SGPSector::FromShortString(ST::string(raw), 0);
			if (probe.IsValid())
			{
				const UINT8 sectorByte = probe.AsByte();
				for (const auto* d : dests)
				{
					if (d->getDeliverySector() == sectorByte) { match = d; break; }
				}
			}
		}

		if (!match)
		{
			// Name match: exact then unique prefix.
			const ShippingDestinationModel* exact = nullptr;
			const ShippingDestinationModel* prefix = nullptr;
			std::size_t prefixHits = 0;
			for (const auto* d : dests)
			{
				const std::string n = lower(d->name.to_std_string());
				if (n == raw) { exact = d; break; }
				if (n.rfind(raw, 0) == 0) { prefix = d; ++prefixHits; }
			}
			match = exact ? exact : (prefixHits == 1 ? prefix : nullptr);
		}

		if (!match)
		{
			Console_Println(ST::format(
				"unknown destination: {} (try a city name, sector id, "
				"or 'default')", raw));
			return;
		}

		BobbyR_SetSelectedCity(static_cast<INT8>(match->locationId));

		Console_Println(ST::format(
			"Shipping to {} ({}). Overnight: ${}/lb. 2-day: ${}/lb. "
			"Standard: ${}/lb.",
			match->name,
			SGPSector::FromSectorID(match->getDeliverySector(), 0).AsShortString(),
			match->chargeRateOverNight,
			match->chargeRate2Days,
			match->chargeRateStandard));
		if (!match->canDeliver)
		{
			Console_Println(
				"  (Bobby Ray's currently cannot deliver here.)");
		}
	}

	// ----- bobbyr speed -----------------------------------------------

	void cmdSpeed(const std::vector<std::string>& args)
	{
		if (!unlockGate()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: bobbyr speed <standard|overnight|express>");
			return;
		}
		const std::string s = lower(args[2]);
		UINT8 speed;
		if      (s == "overnight" || s == "1day" || s == "1d")
			speed = 0;
		else if (s == "express" || s == "2day" || s == "2d" ||
			 s == "twoday" || s == "business")
			speed = 1;
		else if (s == "standard" || s == "std" || s == "ground")
			speed = 2;
		else
		{
			Console_Println(ST::format(
				"unknown speed: {} (try standard, overnight, or express)",
				args[2]));
			return;
		}

		BobbyR_SetSelectedSpeed(speed);
		const auto st = BobbyR_GetOrderState();
		Console_Println(ST::format(
			"Speed: {}. Shipping cost: {}. Grand total: {}.",
			speedName(speed),
			SPrintMoney(static_cast<INT32>(st.shippingCost)),
			SPrintMoney(static_cast<INT32>(st.grandTotal))));
	}

	// ----- bobbyr status ----------------------------------------------

	void cmdStatus(const std::vector<std::string>&)
	{
		if (!unlockGate()) return;
		const auto st = BobbyR_GetOrderState();

		const UINT8 lines = cartUsedSlots();
		Console_Println(ST::format(
			"Cart: {} line{}, {} subtotal, {} lb.",
			lines, lines == 1 ? "" : "s",
			SPrintMoney(static_cast<INT32>(st.subtotal)),
			ST::format("{2.1f}", st.packageWeight / 10.0)));

		if (st.selectedCity != -1)
		{
			auto dest = GCM->getShippingDestination(st.selectedCity);
			Console_Println(ST::format(
				"Ship to: {} ({}). Speed: {} ({}).",
				dest->name,
				SGPSector::FromSectorID(dest->getDeliverySector(), 0).AsShortString(),
				speedName(st.selectedSpeed),
				SPrintMoney(static_cast<INT32>(st.shippingCost))));
			Console_Println(ST::format(
				"Grand total: {}. Cash on hand: {}.",
				SPrintMoney(static_cast<INT32>(st.grandTotal)),
				SPrintMoney(LaptopSaveInfo.iCurrentBalance)));
		}
		else
		{
			Console_Println(
				"Ship to: (not set — try 'bobbyr ship <city>').");
			Console_Println(ST::format(
				"Cash on hand: {}.",
				SPrintMoney(LaptopSaveInfo.iCurrentBalance)));
		}

		if (st.confirmModalUp)
		{
			Console_Println(
				"Order placed — confirm overlay is up. "
				"'bobbyr cancel' or any input dismisses.");
		}
	}

	// ----- bobbyr checkout --------------------------------------------

	void cmdCheckout(const std::vector<std::string>&)
	{
		if (!unlockGate()) return;
		ST::string fail;
		const INT32 shipmentIdx = BobbyR_PlaceOrder(fail);
		if (shipmentIdx < 0)
		{
			Console_Println(ST::format("checkout refused: {}", fail));
			return;
		}

		const auto& s = gpNewBobbyrShipments[shipmentIdx];
		auto dest = GCM->getShippingDestination(s.ubDeliveryLoc);
		const UINT8 days = speedToDaysAhead(s.ubDeliveryMethod);

		Console_Println(ST::format(
			"Order placed. {} item{} shipping to {} ({}), arrives day {} "
			"morning. Balance: {}.",
			s.ubNumberPurchases, s.ubNumberPurchases == 1 ? "" : "s",
			dest->name, speedName(s.ubDeliveryMethod),
			s.uiOrderedOnDayNum + days,
			SPrintMoney(LaptopSaveInfo.iCurrentBalance)));

		// The post-order overlay is briefly up; auto-dismiss so the user
		// can keep using the verbs without an unseen modal blocking GUI
		// state on next render.
		BobbyR_DismissConfirmOverlay();
	}

	// ----- bobbyr cancel ----------------------------------------------

	void cmdCancel(const std::vector<std::string>&)
	{
		if (!unlockGate()) return;
		const auto st = BobbyR_GetOrderState();
		if (st.confirmModalUp)
		{
			BobbyR_DismissConfirmOverlay();
			Console_Println("Confirm overlay dismissed.");
			return;
		}
		Console_Println("(no confirm overlay up; nothing to cancel)");
		cmdStatus({});
	}

	// ----- bobbyr page ------------------------------------------------

	void cmdPage(const std::vector<std::string>& args)
	{
		if (!laptopGate()) return;
		if (!unlockGate()) return;
		if (args.size() < 3)
		{
			Console_Println(
				"usage: bobbyr page <home|guns|ammo|armor|misc|used|"
				"order|shipments>");
			return;
		}
		const std::string p = lower(args[2]);
		LaptopMode target;
		if      (p == "home")      target = LAPTOP_MODE_BOBBY_R;
		else if (p == "guns")      target = LAPTOP_MODE_BOBBY_R_GUNS;
		else if (p == "ammo")      target = LAPTOP_MODE_BOBBY_R_AMMO;
		else if (p == "armor" ||
		         p == "armour")    target = LAPTOP_MODE_BOBBY_R_ARMOR;
		else if (p == "misc")      target = LAPTOP_MODE_BOBBY_R_MISC;
		else if (p == "used")      target = LAPTOP_MODE_BOBBY_R_USED;
		else if (p == "order" ||
		         p == "mail" ||
		         p == "mailorder") target = LAPTOP_MODE_BOBBY_R_MAILORDER;
		else if (p == "shipments") target = LAPTOP_MODE_BOBBYR_SHIPMENTS;
		else
		{
			Console_Println(ST::format("unknown page: {}", args[2]));
			return;
		}

		// If the user is not already on Bobby Ray's, route through the
		// front plaques first so EnterBobbyR fires (sets bookmark, etc.).
		const bool onBobbyR =
			guiCurrentLaptopMode == LAPTOP_MODE_BOBBY_R ||
			guiCurrentLaptopMode == LAPTOP_MODE_BOBBY_R_GUNS ||
			guiCurrentLaptopMode == LAPTOP_MODE_BOBBY_R_AMMO ||
			guiCurrentLaptopMode == LAPTOP_MODE_BOBBY_R_ARMOR ||
			guiCurrentLaptopMode == LAPTOP_MODE_BOBBY_R_MISC ||
			guiCurrentLaptopMode == LAPTOP_MODE_BOBBY_R_USED ||
			guiCurrentLaptopMode == LAPTOP_MODE_BOBBY_R_MAILORDER ||
			guiCurrentLaptopMode == LAPTOP_MODE_BOBBYR_SHIPMENTS;
		if (!onBobbyR)
		{
			GoToWebPage(BOBBYR_BOOKMARK);
		}
		guiCurrentLaptopMode = target;
		Console_Println(ST::format("Loading {} page …", p));
	}

	// ----- bobbyr shipments / shipment --------------------------------

	void cmdShipments(const std::vector<std::string>&)
	{
		if (!unlockGate()) return;
		std::size_t active = 0;
		for (const auto& s : gpNewBobbyrShipments)
			if (s.fActive) ++active;

		if (active == 0)
		{
			Console_Println("No pending shipments.");
			return;
		}

		Console_Println(ST::format("{} pending shipment{}:",
			active, active == 1 ? "" : "s"));

		std::size_t tag = 1;
		for (std::size_t i = 0; i < gpNewBobbyrShipments.size(); ++i)
		{
			const auto& s = gpNewBobbyrShipments[i];
			if (!s.fActive) continue;
			auto dest = GCM->getShippingDestination(s.ubDeliveryLoc);
			const UINT8 days = speedToDaysAhead(s.ubDeliveryMethod);
			Console_Println(ST::format(
				"  s{}  ordered day {}, to {}, {}, {} lb, {} item{}, "
				"ETA day {} morning.",
				tag++,
				s.uiOrderedOnDayNum,
				dest->name,
				speedName(s.ubDeliveryMethod),
				ST::format("{2.1f}", s.uiPackageWeight / 10.0),
				s.ubNumberPurchases,
				s.ubNumberPurchases == 1 ? "" : "s",
				s.uiOrderedOnDayNum + days));
		}
	}

	void cmdShipment(const std::vector<std::string>& args)
	{
		if (!unlockGate()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: bobbyr shipment <sN>");
			return;
		}
		std::string s = lower(args[2]);
		if (!s.empty() && s[0] == 's') s = s.substr(1);
		long n;
		if (!parseInt(s, n) || n < 1)
		{
			Console_Println(ST::format(
				"usage: bobbyr shipment <sN>; got: {}", args[2]));
			return;
		}

		// Walk the active shipments to map sN -> array index.
		std::size_t tag = 0;
		std::size_t found = static_cast<std::size_t>(-1);
		for (std::size_t i = 0; i < gpNewBobbyrShipments.size(); ++i)
		{
			if (!gpNewBobbyrShipments[i].fActive) continue;
			if (++tag == static_cast<std::size_t>(n)) { found = i; break; }
		}
		if (found == static_cast<std::size_t>(-1))
		{
			Console_Println(ST::format(
				"shipment s{} not found ('bobbyr shipments' to list)", n));
			return;
		}

		const auto& sh = gpNewBobbyrShipments[found];
		auto dest = GCM->getShippingDestination(sh.ubDeliveryLoc);
		const UINT8 days = speedToDaysAhead(sh.ubDeliveryMethod);
		Console_Println(ST::format(
			"Shipment s{}: ordered day {}, to {} ({}), {}, ETA day {} "
			"morning. {} lb.",
			n, sh.uiOrderedOnDayNum, dest->name,
			SGPSector::FromSectorID(dest->getDeliverySector(), 0).AsShortString(),
			speedName(sh.ubDeliveryMethod),
			sh.uiOrderedOnDayNum + days,
			ST::format("{2.1f}", sh.uiPackageWeight / 10.0)));

		for (UINT8 i = 0; i < sh.ubNumberPurchases; ++i)
		{
			const auto& p = sh.BobbyRayPurchase[i];
			const ItemModel* item = GCM->getItem(p.usItemIndex);
			Console_Println(ST::format(
				"  {} × {}{}",
				p.ubNumberPurchased,
				item ? item->getBobbyRaysName() : ST::string("?"),
				p.fUsed ? ST::format(" (used Q{}%)", p.bItemQuality) :
					ST::string()));
		}
	}
}

namespace
{
	struct BobbyrSub
	{
		const char* name;
		void      (*fn)(const std::vector<std::string>&);
	};

	// "rm" is a short alias for "remove" — repeated entry over an alias
	// field so each row stays a self-contained one-liner.
	const BobbyrSub kBobbyrSubs[] =
	{
		{ "list",      &cmdList      },
		{ "show",      &cmdShow      },
		{ "add",       &cmdAdd       },
		{ "remove",    &cmdRemove    },
		{ "rm",        &cmdRemove    },
		{ "cart",      &cmdCart      },
		{ "clear",     &cmdClear     },
		{ "ship",      &cmdShip      },
		{ "speed",     &cmdSpeed     },
		{ "status",    &cmdStatus    },
		{ "checkout",  &cmdCheckout  },
		{ "cancel",    &cmdCancel    },
		{ "page",      &cmdPage      },
		{ "shipments", &cmdShipments },
		{ "shipment",  &cmdShipment  },
	};
}

void Cmd_Bobbyr(const std::vector<std::string>& args)
{
	if (args.size() < 2) { cmdSummary(args); return; }

	const std::string sub = lower(args[1]);
	for (const auto& e : kBobbyrSubs)
	{
		if (sub == e.name) { e.fn(args); return; }
	}

	std::string list;
	for (const auto& e : kBobbyrSubs)
	{
		if (!list.empty()) list += ", ";
		list += e.name;
	}
	Console_Println(ST::format(
		"unknown subcommand: bobbyr {} (try 'bobbyr' alone, or one of: {})",
		args[1], list));
}
