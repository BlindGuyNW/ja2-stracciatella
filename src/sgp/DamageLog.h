#ifndef SGP_DAMAGE_LOG_H_
#define SGP_DAMAGE_LOG_H_

#include "Types.h"

#include <string_theory/string>

/* Ring buffer of recent combat events for the console `damage` verb.
 *
 * The engine has no native "what just happened in this fight?" log --
 * combat feedback lives in transient ScreenMsg lines, sound effects,
 * and the rendered damage floaters above sprites. None of that is
 * reviewable, and the SR user has no way to read back what happened
 * off-screen. This buffer captures four kinds of event:
 *
 *   SOLDIER_HIT -- a soldier took damage (attack or non-attack).
 *   STRUCTURE   -- a wall / fence / tree / vehicle / etc. was damaged,
 *                  destroyed, or ignited via DamageStructure.
 *   WINDOW      -- glass cracked or shattered via WindowHit.
 *   LOCK        -- a door lock was kicked or blown.
 *
 * Bullet hits on locks and explosive lock destruction already announce
 * themselves through ScreenMsg (LOCK_HAS_BEEN_HIT / LOCK_HAS_BEEN_DESTROYED
 * in Keys.cc, LOS.cc) which our ScreenMsg->AX_Say hook narrates live;
 * the LOCK kind is for review and for the kick path which does not fire
 * a ScreenMsg in vanilla.
 *
 * Capacity is 32 on purpose: this is "what just happened in this
 * fight", not a campaign history. Old records are evicted silently as
 * the buffer fills. Pushes and reads both happen on the game thread,
 * during damage resolution and console dispatch respectively -- same
 * thread, no locking.
 *
 * Offsets render at read time from the target's gridno against the
 * selected merc's current position; we deliberately do not freeze a
 * cartesian offset at push time, so the user gets useful directions
 * even after moving. The actor and target SOLDIERTYPE* pointers are
 * not stored -- they can dangle after death or a sector unload -- so
 * names and gridnos are captured up-front. */

namespace DamageLog
{
	enum Kind : UINT8
	{
		KIND_SOLDIER_HIT = 0,
		KIND_STRUCTURE   = 1,
		KIND_WINDOW      = 2,
		KIND_LOCK        = 3,
	};

	enum NounClass : UINT8
	{
		NOUN_OTHER     = 0,
		NOUN_WALL      = 1,  // STRUCTURE_WALLSTUFF & !WALLNWINDOW
		NOUN_FENCE     = 2,  // STRUCTURE_FENCE / WIREFENCE
		NOUN_DOOR      = 3,  // STRUCTURE_ANYDOOR (the door structure itself)
		NOUN_TREE      = 4,  // STRUCTURE_TREE
		NOUN_VEHICLE   = 5,  // STRUCTURE_VEHICLE
		NOUN_CONTAINER = 6,  // STRUCTURE_OPENABLE & !STRUCTURE_DOOR (lockers, cabinets)
		NOUN_FURNITURE = 7,  // MATERIAL_FURNITURE
		NOUN_SWITCH    = 8,  // STRUCTURE_SWITCH
		NOUN_LIGHT     = 9,  // STRUCTURE_LIGHTSOURCE
		NOUN_EXPLOSIVE = 10, // STRUCTURE_EXPLOSIVE (gas tank / red barrel)
	};

	enum Disposition : UINT8
	{
		DISP_NONE          = 0,
		DISP_DAMAGED       = 1, // structure HP > 0, graphic swapped
		DISP_DESTROYED     = 2, // structure removed
		DISP_IGNITED       = 3, // STRUCTURE_EXPLOSIVE secondary-detonated
		DISP_CRACKED       = 4, // window: partner-swap, stages remain
		DISP_SHATTERED     = 5, // window: terminal partner
		DISP_LOCK_SMASHED  = 6, // kick success
		DISP_LOCK_BLOWN    = 7, // shaped-charge success
		DISP_LOCK_SHOT     = 8, // bullet damaged or destroyed the lock
	};

	enum Cause : UINT8
	{
		CAUSE_NONE          = 0,
		CAUSE_GUNFIRE       = 1,
		CAUSE_EXPLOSION     = 2, // grenade / rocket / mortar / mine / area
		CAUSE_KICK          = 3, // AttemptToSmashDoor
		CAUSE_SHAPED_CHARGE = 4, // AttemptToBlowUpLock
		CAUSE_THROWN        = 5, // thrown-object collision (Physics.cc)
		CAUSE_CROWBAR       = 6, // AttemptToCrowbarLock
	};

	struct Record
	{
		UINT32 timestamp_ms;
		Kind   kind;

		// Actor (whoever caused the event). Empty when no actor (bleed,
		// fall, engine-owned booby trap, etc.).
		ST::string attacker_name;
		INT8       attacker_team;    // -1 when no actor
		bool       attacker_visible;
		INT16      attacker_gridno;  // -1 when no actor

		// Target. For SOLDIER_HIT this is the struck merc; for the
		// non-soldier kinds, target_name is empty and target_team is -1
		// while target_gridno/level locate the event tile.
		ST::string target_name;
		INT8       target_team;
		bool       target_visible;   // soldier opplist OR IsKnownTile
		INT16      target_gridno;
		INT8       target_level;

		// Weapon / triggering item. For gunfire: the weapon. For
		// explosion: the explosive item (grenade, mortar shell). For
		// kick / bleed / fall: NOTHING.
		UINT16     weapon_index;

		// SOLDIER_HIT payload.
		UINT8      reason;           // TAKE_DAMAGE_*
		UINT8      hit_location;     // AIM_SHOT_*, 0 for splash
		UINT8      special;
		INT16      damage_life;
		INT16      damage_breath;
		INT8       life_before, life_after, life_max;
		INT8       breath_before, breath_after, breath_max;
		bool       killed;
		bool       knocked_out;

		// Non-soldier payload.
		Disposition disp;
		Cause       cause;
		UINT16      structure_id;    // for same-event dedup; 0 for non-structure kinds
		NounClass   noun_class;
		UINT8       material;        // MATERIAL_* (gubMaterialArmour index)
	};

	void          Push(const Record& rec);
	UINT32        Size(void);
	const Record* Get(UINT32 newest_index);
	void          Reset(void);

	// Newest record, or nullptr if empty. Used by push wrappers to
	// dedup repeat events on the same structure within a short window.
	const Record* PeekNewest(void);
}

#endif // SGP_DAMAGE_LOG_H_
