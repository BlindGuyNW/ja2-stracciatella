#include "Console_Dispatch.h"
#include "Console_Action.h"
#include "Console_Aim.h"
#include "Console_Bobbyr.h"
#include "Console_Cheat.h"
#include "Console_Email.h"
#include "Console_Gui.h"
#include "Console_Imp.h"
#include "Console_Map.h"
#include "Console_Query.h"
#include "Console_Rows.h"
#include "Console_Text.h"
#include "Console_Web.h"

#include "Console.h"

#include <cctype>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	using ArgList = std::vector<std::string>;
	using Handler = void (*)(const ArgList&);

	struct Entry
	{
		const char* verb;
		Handler     fn;
		const char* help;
	};

	void cmdHelp(const ArgList&); // forward

	const Entry kTable[] =
	{
		{ "help",      &cmdHelp,        "list commands"                                          },
		{ "sector",    &Cmd_Sector,     "sector name, time, turn phase"                          },
		{ "merc",      &Cmd_Merc,       "merc [name] — life, breath, AP, stance, held weapon"    },
		{ "nearby",    &Cmd_Nearby,     "nearby [enemies|mercs|civs|items|doors|containers|exits|hazards|mines|unexplored|all] [N]" },
		{ "look",      &Cmd_Look,       "look [<dir> [N]] — scan a direction (or all 4 cardinals)" },
		{ "tile",      &Cmd_Tile,       "tile <target> — terrain and occupant"                   },
		{ "cth",       &Cmd_Cth,        "cth <target> — chance-to-hit at aim 0..4"               },
		{ "cover",     &Cmd_Cover,      "cover [target] — cover at a tile vs known enemies"      },
		{ "inventory",  &Cmd_Inventory,  "inventory [name] — slots, ammo, and reload sources"      },
		{ "examine",    &Cmd_Examine,    "examine <slot> — describe an inventory item (s1..s19)"   },
		{ "path",       &Cmd_Path,       "path <target> — route AP cost from selected"            },
		{ "select",     &Cmd_Select,     "select <name> — set selected merc"                      },
		{ "move",       &Cmd_Move,       "move <target> — walk to a target (opens doors)"         },
		{ "move-all",   &Cmd_MoveAll,    "move-all <target> — whole squad walks (real time)"      },
		{ "turn",       &Cmd_Turn,       "turn <n|ne|...> | turn <target> — face that way"        },
		{ "stance",     &Cmd_Stance,     "stance <p|c|s> — prone / crouch / stand"                },
		{ "fire",       &Cmd_Fire,       "fire <count> <target>"                                  },
		{ "reload",     &Cmd_Reload,     "reload [name] — top up the held gun from carried ammo"  },
		{ "pickup",     &Cmd_Pickup,     "pickup [target] — walk to a tile and grab visible items" },
		{ "bandage",    &Cmd_Bandage,    "bandage [name] — apply a first aid kit (defaults to self)" },
		{ "swap-hands", &Cmd_SwapHands,  "swap-hands — swap main and off hand"                    },
		{ "swap",       &Cmd_Swap,       "swap <slot> <slot> — move an item between two slots"    },
		{ "drop",       &Cmd_Drop,       "drop <slot> — drop a slot's item to the ground"         },
		{ "give",       &Cmd_Give,       "give <slot> <name> — hand a slot's item to a teammate"  },
		{ "talk",       &Cmd_Talk,       "talk <target> — start conversation; talk <approach|skip|done> drives an open panel" },
		{ "exit",       &Cmd_Exit,       "exit <n|s|e|w> — leave the sector through that side"   },
		{ "end-turn",   &Cmd_EndTurn,    "end-turn"                                               },
		{ "email",     &Cmd_Email,      "email | email unread | email <id> — inbox (laptop only)" },
		{ "aim",       &Cmd_Aim,        "aim | aim members | aim list | aim merc <name|aN> | aim show <name|aN> | aim contact | aim length day|week|biweek | aim gear on|off | aim status | aim authorize | aim cancel | aim archives [name|N] | aim history [N] | aim policies [N] | aim links [bobby|funeral|insurance]" },
		{ "imp",       &Cmd_Imp,        "imp [goto|name|nickname|gender|question|answer|confirm|prev|next|traits|trait|stats|stat|portrait|voice|done|hire]" },
		{ "bobbyr",    &Cmd_Bobbyr,     "bobbyr [list|show|add|remove|cart|clear|ship|speed|status|checkout|cancel|page|shipments|shipment]" },
		{ "team",      &Cmd_Team,       "team | team list [filter] [sort] | team merc <name|tN> | team select <name|tN> [+|-] | team sleep <name|tN> <on|off>" },
		{ "map",       &Cmd_Map,        "map | map sector <id> | map list <towns|mines|sams|militia|enemies> | map town <name> | map mine <town> | map level <0..3> | map move <sector> [from <name>] [keep-path] | map cancel [<name>]" },
		{ "laptop",    &Cmd_Laptop,     "laptop — open the laptop screen"                        },
		{ "tactical",  &Cmd_Tactical,   "tactical — switch to the tactical screen"               },
		{ "quit",      &Cmd_Quit,       "quit — open the options screen (Save / Load / Quit)"    },
		{ "compress",  &Cmd_Compress,   "compress <off|x1|fast|faster|fastest> — time compression" },
		{ "log",       &Cmd_Log,        "log [N] — replay last N strategic messages (default 9)" },
		{ "text",      &Cmd_Text,       "text | text <id> | text <id> <value> — read/write text input fields" },
		{ "web",       &Cmd_Web,        "web | web <name> — list or open a bookmarked laptop site (laptop only)" },
		{ "g",         &Cmd_Gui,        "g list | g click <id> — enumerate or click visible buttons" },
		{ "r",         &Cmd_Rows,       "r list | r click <id> — enumerate or click rendered text rows" },
		{ "cheat",     &Cmd_Cheat,      "cheat | cheat <name> — dev unlocks (e.g. 'cheat bobbyr')"  },
	};

	void cmdHelp(const ArgList&)
	{
		Console_Println("Commands:");
		for (const auto& e : kTable)
		{
			Console_Println(ST::format("  {} — {}", e.verb, e.help));
		}
		Console_Println("Targets are: a name (e.g. ivan), a direction+steps (e.g. ne 5),");
		Console_Println("or a col,row coordinate (e.g. 80,90).");
	}

	ArgList tokenize(const std::string& line)
	{
		ArgList out;
		std::size_t i = 0;
		while (i < line.size())
		{
			while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
			if (i >= line.size()) break;
			const std::size_t start = i;
			while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
			out.emplace_back(line.substr(start, i - start));
		}
		return out;
	}

	void dispatch(const std::string& line)
	{
		auto toks = tokenize(line);
		if (toks.empty()) return;

		// Echo the command back to the console so the screen reader can
		// confirm what the parser saw, and so the user can scroll back
		// through a clean transcript.
		Console_Println(ST::format("> {}", line));

		for (const auto& e : kTable)
		{
			if (toks[0] == e.verb)
			{
				e.fn(toks);
				return;
			}
		}
		Console_Println(ST::format("unknown command: {} (try 'help')", toks[0]));
	}
}

void ConsoleDispatch_Tick(void)
{
	Console_Drain(&dispatch);
}
