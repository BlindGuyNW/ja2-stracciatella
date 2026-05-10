#ifndef SGP_DAMAGE_LOG_H_
#define SGP_DAMAGE_LOG_H_

#include "Types.h"

#include <string_theory/string>

/* Ring buffer of recent damage events for the console `damage` verb.
 *
 * The engine has no native "what just hit whom?" log -- combat feedback
 * lives entirely in transient ScreenMsg lines and the rendered damage
 * floaters above sprites. Neither is reviewable, and the player has no
 * way to read back what an off-screen merc just took. This buffer is
 * populated by hooks at the central damage funnels (EVENT_SoldierGotHit
 * for attacks; SoldierTakeDamage for non-attack causes such as bleed,
 * fall, or gas) and read by Cmd_Damage. Pushes happen on the game
 * thread during damage resolution; reads happen on the game thread
 * during console dispatch. Same thread, no locking.
 *
 * Capacity is small (32) on purpose: this is a "what just happened in
 * this fight" log, not a campaign history. Old records are evicted
 * silently when the buffer fills.
 *
 * Fields are raw integers (team / weapon / body-part ids) rather than
 * resolved names so the formatter can localize and visibility-gate
 * however it likes; only the names are captured up-front because the
 * SOLDIERTYPE pointers can become invalid (death, sector change). */

namespace DamageLog
{
	struct Record
	{
		UINT32     timestamp_ms;       // GetJA2Clock() at moment of hit

		ST::string attacker_name;      // empty when no attacker (bleed / fall)
		INT8       attacker_team;      // -1 when no attacker
		bool       attacker_visible;   // known to OUR_TEAM at hit time

		ST::string target_name;
		INT8       target_team;
		bool       target_visible;     // OUR_TEAM merc, or known via opplist

		UINT16     weapon_index;       // NOTHING for non-weapon damage
		UINT8      reason;             // TAKE_DAMAGE_*
		UINT8      hit_location;       // AIM_SHOT_HEAD/TORSO/LEGS, 0 = none
		UINT8      special;            // FIRE_WEAPON_*_SPECIAL flag

		INT16      damage_life;        // life points deducted (post-clamp)
		INT16      damage_breath;      // breath points deducted (BP, hundredths)

		INT8       life_before;
		INT8       life_after;
		INT8       life_max;
		INT8       breath_before;      // 0..100 percent
		INT8       breath_after;
		INT8       breath_max;

		INT16      range_tiles;        // PythSpacesAway, -1 if no attacker
		UINT8      direction;          // NORTH..NORTHWEST, 0xFF if N/A

		bool       killed;             // life dropped to 0 by this hit
		bool       knocked_out;        // life crossed below CONSCIOUSNESS
	};

	void          Push(const Record& rec);
	UINT32        Size(void);
	const Record* Get(UINT32 newest_index);
	void          Reset(void);
}

#endif // SGP_DAMAGE_LOG_H_
