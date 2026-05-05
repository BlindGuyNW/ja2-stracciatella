#include "Console_Dispatch.h"
#include "Console_Action.h"
#include "Console_Email.h"
#include "Console_Gui.h"
#include "Console_Query.h"
#include "Console_Rows.h"

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
		{ "nearby",    &Cmd_Nearby,     "nearby [enemies|mercs|civs|items|doors|containers|exits|unexplored|all] [N]" },
		{ "look",      &Cmd_Look,       "look [<dir> [N]] — scan a direction (or all 4 cardinals)" },
		{ "tile",      &Cmd_Tile,       "tile <target> — terrain and occupant"                   },
		{ "cth",       &Cmd_Cth,        "cth <target> — chance-to-hit at aim 0..4"               },
		{ "inventory", &Cmd_Inventory,  "inventory [name] — slots, ammo, and reload sources"      },
		{ "path",      &Cmd_Path,       "path <target> — route AP cost from selected"            },
		{ "select",    &Cmd_Select,     "select <name> — set selected merc"                      },
		{ "move",      &Cmd_Move,       "move <target> — walk to a target (opens doors)"         },
		{ "move-all",  &Cmd_MoveAll,    "move-all <target> — whole squad walks (real time)"      },
		{ "turn",      &Cmd_Turn,       "turn <n|ne|...> | turn <target> — face that way"        },
		{ "stance",    &Cmd_Stance,     "stance <p|c|s> — prone / crouch / stand"                },
		{ "fire",      &Cmd_Fire,       "fire <count> <target>"                                  },
		{ "reload",    &Cmd_Reload,     "reload [name] — top up the held gun from carried ammo"  },
		{ "pickup",    &Cmd_Pickup,     "pickup [target] — walk to a tile and grab visible items" },
		{ "bandage",   &Cmd_Bandage,    "bandage [name] — apply a first aid kit (defaults to self)" },
		{ "end-turn",  &Cmd_EndTurn,    "end-turn"                                               },
		{ "email",     &Cmd_Email,      "email | email unread | email <id> — inbox (laptop only)" },
		{ "g",         &Cmd_Gui,        "g list | g click <id> — enumerate or click visible buttons" },
		{ "r",         &Cmd_Rows,       "r list | r click <id> — enumerate or click rendered text rows" },
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
