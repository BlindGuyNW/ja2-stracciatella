#include "Console_Text.h"

#include "Text_Input.h"

#include "Console.h"

#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	// IMP screens use 1–2 fields; AIM/Bobby Ray/save-name will be similar.
	// We probe a small id range rather than reaching into Text_Input.cc's
	// static node list. Fields are allocated sequentially from 0, so the
	// first few ids cover every screen we care about.
	constexpr UINT8 kProbeMax = 8;

	bool parseId(const std::string& s, UINT8& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0' || v < 0 || v > 255) return false;
		out = static_cast<UINT8>(v);
		return true;
	}

	// Joins arg tokens 2..end with single spaces. The dispatcher tokenises
	// on whitespace, so a value typed as `text 0 my full name` arrives as
	// {"text","0","my","full","name"}. We rejoin everything past the id.
	ST::string joinFromArg2(const std::vector<std::string>& args)
	{
		ST::string out;
		for (std::size_t i = 2; i < args.size(); ++i)
		{
			if (i > 2) out += " ";
			out += ST::string(args[i]);
		}
		return out;
	}

	void cmdList()
	{
		if (!TextInputMode())
		{
			Console_Println("(no text input fields active)");
			return;
		}

		const INT16 active = GetActiveFieldID();
		for (UINT8 id = 0; id < kProbeMax; ++id)
		{
			ST::string s = GetStringFromField(id);
			const char* tag = (active >= 0 && id == active) ? " [active]" : "";
			Console_Println(ST::format("  {} — \"{}\"{}", id, s, tag));
		}
		Console_Println("(showing ids 0..7; empty rows may be unallocated or just blank)");
	}
}

void Cmd_Text(const std::vector<std::string>& args)
{
	if (!TextInputMode())
	{
		Console_Println("(no text input fields active)");
		return;
	}

	if (args.size() < 2) { cmdList(); return; }

	UINT8 id;
	if (!parseId(args[1], id))
	{
		Console_Println(ST::format(
			"unknown subcommand: text {} (try 'text', 'text <id>', 'text <id> <value>')",
			args[1]));
		return;
	}

	if (args.size() == 2)
	{
		Console_Println(ST::format("  {} — \"{}\"", id, GetStringFromField(id)));
		return;
	}

	const ST::string value = joinFromArg2(args);
	SetInputFieldString(id, value);
	Console_Println(ST::format("set {} to \"{}\"", id, GetStringFromField(id)));
}
