#include "Console_Cheat.h"

#include "Laptop.h"
#include "LaptopSave.h"
#include "Types.h"

#include "Console.h"

#include <cctype>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	std::string lower(std::string s)
	{
		for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	// Mirrors the engine's natural unlock at Player_Command.cc:91 (flag
	// flip on liberating the shipping destination), plus the bookmark add
	// that BobbyR.cc:226 performs the first time the player visits the
	// site. Without the bookmark, `web bobbyr` refuses to load the page.
	void cheatBobbyR()
	{
		const bool wasOpen = LaptopSaveInfo.fBobbyRSiteCanBeAccessed;
		LaptopSaveInfo.fBobbyRSiteCanBeAccessed = TRUE;
		SetBookMark(BOBBYR_BOOKMARK);
		Console_Println(wasOpen
			? "Bobby Ray's was already accessible; bookmark refreshed."
			: "Bobby Ray's unlocked. Use 'web bobbyr' to open the site.");
	}

	struct Entry
	{
		const char* name;
		void      (*fn)();
		const char* help;
	};

	const Entry kCheats[] =
	{
		{ "bobbyr", &cheatBobbyR, "unlock Bobby Ray's Guns and Things (skips the Drassen-airport gate)" },
	};

	void listCheats()
	{
		Console_Println("Cheats:");
		for (const auto& c : kCheats)
		{
			Console_Println(ST::format("  cheat {} — {}", c.name, c.help));
		}
	}
}

void Cmd_Cheat(const std::vector<std::string>& args)
{
	if (args.size() < 2) { listCheats(); return; }

	const std::string want = lower(args[1]);
	for (const auto& c : kCheats)
	{
		if (want == c.name) { c.fn(); return; }
	}
	Console_Println(ST::format("unknown cheat: {} (try 'cheat')", args[1]));
}
