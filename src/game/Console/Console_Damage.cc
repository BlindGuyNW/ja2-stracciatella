#include "Console_Damage.h"

#include "Console.h"
#include "DamageLog.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "Game_Clock.h"
#include "ItemModel.h"
#include "Items.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "Text.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_theory/format>
#include <string_theory/string>

namespace
{
	const char* directionWord(UINT8 dir)
	{
		switch (dir)
		{
			case NORTH:     return "N";
			case NORTHEAST: return "NE";
			case EAST:      return "E";
			case SOUTHEAST: return "SE";
			case SOUTH:     return "S";
			case SOUTHWEST: return "SW";
			case WEST:      return "W";
			case NORTHWEST: return "NW";
			default:        return "?";
		}
	}

	// Maps the damage `reason` to a present-tense action verb. The action
	// is phrased so it reads naturally with or without an attacker:
	//   "Hans shot Ivan ..."          (attacker present)
	//   "Hans bleeding ..."           (no attacker)
	const char* actionWord(UINT8 reason, bool hasAttacker)
	{
		switch (reason)
		{
			case TAKE_DAMAGE_GUNFIRE:             return "shot";
			case TAKE_DAMAGE_BLADE:               return "stabbed";
			case TAKE_DAMAGE_HANDTOHAND:          return "punched";
			case TAKE_DAMAGE_EXPLOSION:           return hasAttacker ? "blasted" : "caught the blast";
			case TAKE_DAMAGE_STRUCTURE_EXPLOSION: return "caught the structure blast";
			case TAKE_DAMAGE_BLOODLOSS:           return "bleeding";
			case TAKE_DAMAGE_GAS:                 return "gassed";
			case TAKE_DAMAGE_TENTACLES:           return "grabbed by tentacles";
			case TAKE_DAMAGE_OBJECT:              return hasAttacker ? "hit with a thrown object" : "struck by an object";
			case TAKE_DAMAGE_FALLROOF:            return "fell";
			case TAKE_DAMAGE_ELECTRICITY:         return "shocked";
			default:                              return "hit";
		}
	}

	// AIM_SHOT_HEAD=1 / TORSO=2 / LEGS=3 map to the engine's localized
	// hit-location strings (the same TacticalStr entries the targeting
	// cursor uses). 0 (AIM_SHOT_RANDOM) leaves no qualifier — splash
	// damage and indirect causes don't have a meaningful body part.
	ST::string bodyPartWord(UINT8 hitLocation)
	{
		switch (hitLocation)
		{
			case AIM_SHOT_HEAD:  return TacticalStr[HEAD_HIT_LOCATION_STR];
			case AIM_SHOT_TORSO: return TacticalStr[TORSO_HIT_LOCATION_STR];
			case AIM_SHOT_LEGS:  return TacticalStr[LEGS_HIT_LOCATION_STR];
			default:             return ST::string{};
		}
	}

	ST::string weaponWord(UINT16 weaponIndex)
	{
		if (weaponIndex == NOTHING) return ST::string{};
		const ItemModel* item = GCM->getItem(weaponIndex);
		return item != nullptr ? item->getShortName() : ST::string{};
	}

	ST::string formatElapsed(UINT32 nowMs, UINT32 thenMs)
	{
		const UINT32 dms = nowMs >= thenMs ? nowMs - thenMs : 0;
		const UINT32 secs = dms / 1000;
		if (secs < 60)   return ST::format("{}s ago",  secs);
		if (secs < 3600) return ST::format("{}m{}s ago", secs / 60, secs % 60);
		return ST::format("{}h{}m ago", secs / 3600, (secs % 3600) / 60);
	}

	// Visibility-gating policy for default listing:
	//   - target on OUR_TEAM           -> always show (player's merc was hit)
	//   - target visible to OUR_TEAM   -> show (we saw the enemy go down)
	//   - attacker visible to OUR_TEAM -> show (we saw who fired)
	// otherwise the event happened entirely off-screen for the player and
	// is suppressed unless `damage all` is used.
	bool shouldList(const DamageLog::Record& r, bool listAll)
	{
		if (listAll) return true;
		if (r.target_team == OUR_TEAM) return true;
		if (r.target_visible)          return true;
		if (r.attacker_visible)        return true;
		return false;
	}

	ST::string actorPhrase(const ST::string& name, bool visible, INT8 team)
	{
		if (visible && !name.empty()) return name;
		if (team == OUR_TEAM)         return name.empty() ? ST::string{"a teammate"} : name;
		if (team < 0)                 return ST::string{};
		return ST::string{"an unseen attacker"};
	}

	ST::string targetPhrase(const ST::string& name, bool visible, INT8 team)
	{
		if (visible && !name.empty()) return name;
		if (team == OUR_TEAM)         return name.empty() ? ST::string{"a teammate"} : name;
		return ST::string{"an unseen target"};
	}

	void printOneLiner(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		const ST::string when     = formatElapsed(nowMs, r.timestamp_ms);
		const bool       hasAtt   = (r.attacker_team >= 0);
		const ST::string attStr   = actorPhrase(r.attacker_name, r.attacker_visible, r.attacker_team);
		const ST::string tgtStr   = targetPhrase(r.target_name, r.target_visible, r.target_team);
		const ST::string action   = actionWord(r.reason, hasAtt);
		const ST::string part     = bodyPartWord(r.hit_location);
		const ST::string weapon   = weaponWord(r.weapon_index);

		// Lead clause: "<actor> <action> <target>" or "<target> <action>"
		// for non-attack reasons (bleed / fall / gas with no attacker).
		ST::string lead;
		if (hasAtt && !attStr.empty())
		{
			lead = ST::format("{} {} {}", attStr, action, tgtStr);
		}
		else
		{
			// Phrase as "<target> bleeding" / "<target> fell" / etc.
			lead = ST::format("{} {}", tgtStr, action);
		}

		// Qualifier: "(in the head, with M14)" / "(with M14)" / "(in the head)"
		ST::string qualifier;
		if (!part.empty() && !weapon.empty())
		{
			qualifier = ST::format(" (in the {}, with {})", part, weapon);
		}
		else if (!part.empty())
		{
			qualifier = ST::format(" (in the {})", part);
		}
		else if (!weapon.empty())
		{
			qualifier = ST::format(" (with {})", weapon);
		}

		// Range suffix when we have a real attacker.
		ST::string range;
		if (hasAtt && r.range_tiles >= 0)
		{
			range = ST::format(", {} tiles {}",
				static_cast<int>(r.range_tiles),
				directionWord(r.direction));
		}

		// Damage and life/breath state.
		ST::string dmg;
		if (r.damage_breath != 0)
		{
			dmg = ST::format("{} dmg, {} breath", static_cast<int>(r.damage_life),
				static_cast<int>(r.damage_breath));
		}
		else
		{
			dmg = ST::format("{} dmg", static_cast<int>(r.damage_life));
		}

		ST::string outcome;
		if (r.killed)            outcome = ", killed";
		else if (r.knocked_out)  outcome = ", knocked out";

		Console_Println(ST::format("{}. {}: {}{}{}. {}. Life {}/{}{}.",
			slotN, when, lead, qualifier, range, dmg,
			static_cast<int>(r.life_after), static_cast<int>(r.life_max),
			outcome));
	}

	void printDetail(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		const ST::string when    = formatElapsed(nowMs, r.timestamp_ms);
		const ST::string attStr  = actorPhrase(r.attacker_name, r.attacker_visible, r.attacker_team);
		const ST::string tgtStr  = targetPhrase(r.target_name, r.target_visible, r.target_team);
		const ST::string action  = actionWord(r.reason, r.attacker_team >= 0);
		const ST::string part    = bodyPartWord(r.hit_location);
		const ST::string weapon  = weaponWord(r.weapon_index);

		Console_Println(ST::format("damage #{} ({}):", slotN, when));
		if (r.attacker_team >= 0)
		{
			Console_Println(ST::format("  attacker: {}", attStr));
			if (r.range_tiles >= 0)
			{
				Console_Println(ST::format("  range: {} tiles, direction {}",
					static_cast<int>(r.range_tiles),
					directionWord(r.direction)));
			}
		}
		Console_Println(ST::format("  target: {}", tgtStr));
		Console_Println(ST::format("  action: {}", action));
		if (!weapon.empty()) Console_Println(ST::format("  weapon: {}", weapon));
		if (!part.empty())   Console_Println(ST::format("  body part: {}", part));
		Console_Println(ST::format("  life: {} -> {} (max {})",
			static_cast<int>(r.life_before),
			static_cast<int>(r.life_after),
			static_cast<int>(r.life_max)));
		Console_Println(ST::format("  breath: {} -> {} (max {})",
			static_cast<int>(r.breath_before),
			static_cast<int>(r.breath_after),
			static_cast<int>(r.breath_max)));
		if (r.killed)       Console_Println("  killed.");
		if (r.knocked_out)  Console_Println("  knocked out.");
	}

	bool parseSlot(const std::string& s, UINT32& out)
	{
		if (s.empty()) return false;
		for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
		try
		{
			out = static_cast<UINT32>(std::stoul(s));
			return true;
		}
		catch (...) { return false; }
	}
}

void Cmd_Damage(const std::vector<std::string>& args)
{
	const UINT32 size = DamageLog::Size();
	if (size == 0)
	{
		Console_Println("No damage recorded yet.");
		return;
	}

	const UINT32 nowMs = GetJA2Clock();
	bool listAll = false;

	if (args.size() >= 2)
	{
		std::string sub = args[1];
		std::transform(sub.begin(), sub.end(), sub.begin(),
			[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		if (sub == "all")
		{
			listAll = true;
		}
		else
		{
			UINT32 slot = 0;
			if (parseSlot(sub, slot) && slot >= 1 && slot <= size)
			{
				const DamageLog::Record* r = DamageLog::Get(slot - 1);
				if (r != nullptr)
				{
					printDetail(slot, nowMs, *r);
					return;
				}
			}
			Console_Println(ST::format(
				"damage: unknown argument '{}' (try 'damage', 'damage all', 'damage <N>' where N is 1..{})",
				args[1], static_cast<int>(size)));
			return;
		}
	}

	UINT32 listed = 0;
	for (UINT32 i = 0; i < size; ++i)
	{
		const DamageLog::Record* r = DamageLog::Get(i);
		if (r == nullptr) continue;
		if (!shouldList(*r, listAll)) continue;
		printOneLiner(i + 1, nowMs, *r);
		++listed;
	}

	if (listed == 0)
	{
		Console_Println(listAll
			? "No damage records to show."
			: "No visible damage events. Try 'damage all' to include off-screen events.");
	}
}
