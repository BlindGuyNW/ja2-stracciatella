#include "Console_Web.h"

#include "JAScreens.h"
#include "Laptop.h"
#include "LaptopSave.h"
#include "ScreenIDs.h"

#include "Console.h"
#include "Console_Address.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	struct SiteEntry
	{
		const char* name; // primary name used by `web <name>`
		const char* alias; // secondary name (or nullptr) — e.g. "bobbyrayguns"
		INT32       id;
	};

	// Bookmark ids come from the Laptop.h enum (AIM_BOOKMARK = 0 …
	// INSURANCE_BOOKMARK = 6). Names match the in-game site labels in
	// pBookMarkStrings, lowercased and shortened where needed.
	const SiteEntry kSites[] =
	{
		{ "aim",       nullptr,         AIM_BOOKMARK       },
		{ "bobbyr",    "bobbyrayguns",  BOBBYR_BOOKMARK    },
		{ "imp",       nullptr,         IMP_BOOKMARK       },
		{ "mercs",     "merc",          MERC_BOOKMARK      },
		{ "funeral",   nullptr,         FUNERAL_BOOKMARK   },
		{ "florist",   nullptr,         FLORIST_BOOKMARK   },
		{ "insurance", nullptr,         INSURANCE_BOOKMARK },
	};

	bool laptopGate()
	{
		if (guiCurrentScreen == LAPTOP_SCREEN) return true;
		Console_Println("Open your laptop first.");
		return false;
	}

	bool isBookmarked(INT32 id)
	{
		for (INT32 v : LaptopSaveInfo.iBookMarkList)
		{
			if (v == -1) return false;
			if (v == id) return true;
		}
		return false;
	}

	const SiteEntry* findSite(const std::string& want)
	{
		const std::string w = lower(want);
		const SiteEntry*  hit = nullptr;
		std::size_t       hits = 0;
		for (const auto& s : kSites)
		{
			std::string n  = s.name;
			std::string a  = s.alias ? s.alias : "";
			if (n == w || a == w) return &s; // exact wins
			if (n.rfind(w, 0) == 0 || (!a.empty() && a.rfind(w, 0) == 0))
			{
				hit = &s;
				++hits;
			}
		}
		return hits == 1 ? hit : nullptr;
	}

	void cmdList()
	{
		Console_Println("Bookmarked sites:");
		std::size_t shown = 0;
		for (const auto& s : kSites)
		{
			if (!isBookmarked(s.id)) continue;
			Console_Println(ST::format("  {}{}",
				s.name,
				s.alias ? ST::format(" (alias: {})", s.alias) : ST::string()));
			++shown;
		}
		if (shown == 0)
		{
			Console_Println("(none yet — bookmarks appear as the player's contacted by IMP/AIM/etc.)");
		}
	}
}

void Cmd_Web(const std::vector<std::string>& args)
{
	if (!laptopGate()) return;
	if (args.size() < 2) { cmdList(); return; }

	const SiteEntry* site = findSite(args[1]);
	if (!site)
	{
		Console_Println(ST::format(
			"unknown or ambiguous site: {} (try 'web' to list)", args[1]));
		return;
	}
	if (!isBookmarked(site->id))
	{
		Console_Println(ST::format(
			"site '{}' is not bookmarked yet — wait for the relevant in-game email.",
			site->name));
		return;
	}

	GoToWebPage(site->id);
	Console_Println(ST::format("loading {} …", site->name));
}
