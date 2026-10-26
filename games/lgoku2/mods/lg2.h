// What is known about Legacy of Goku II's internals (European ROM, ALFP).
// Names are ours; the game's source file for this part is CharacterData.cpp.
#ifndef LG2_H
#define LG2_H

#include "mod.h"

enum { LG2_GOHAN, LG2_PICCOLO, LG2_VEGETA, LG2_TRUNKS, LG2_GOKU, LG2_HERCULE, LG2_CHARACTERS };
enum { LG2_STR, LG2_POW, LG2_END, LG2_STATS };

// One per playable character. The ROM holds the starting values at 0x2A868;
// the game copies them into RAM and works on the copy.
struct LG2Char {
	uint16_t hp, maxHp;
	uint16_t ep, maxEp;
	uint8_t sprite;           // which form is shown: index into LG2_ROM_FORMS
	uint8_t selectedAttack;   // 0-3: the energy attack slot chosen with L/R
	uint16_t stat[LG2_STATS]; // 8.8 fixed point: 0x0300 is 3. The game caps each at 100.
	uint32_t xp;
	uint32_t flags;           // bit 0: character is available to the player; bits 3-6: attack slots 0-3 owned
	uint8_t attack[4];        // what is in each attack slot: index into LG2_ROM_ATTACKS
};

// Per character and stat: least and most the stat grows per level, 8.8 fixed point
struct LG2Growth {
	struct { uint16_t min, max; } stat[LG2_STATS];
};

#define LG2_ROM_CHARS 0x2A868      // struct LG2Char[6]
#define LG2_ROM_GROWTH 0x2A910     // struct LG2Growth[6]
#define LG2_ROM_XP_TABLE 0x2A6A0   // uint32_t[50]: total experience needed for level n+1
#define LG2_RAM_CHARDATA 0x030010AC // byte 0: current character; +12: struct LG2Char[6]
#define LG2_RAM_LEVELUP_GAINS 0x0300103D // 3 bytes: stat gains shown by the level-up message
#define LG2_ROM_LEVELUP_BONUS_TEXT 0x6EB404 // "@#Attribute Bonus\nStrength: +%d\n..." (English), the second level-up message
#define LG2_RAM_PLAYER_PTR 0x03001390 // pointer to the player object; 0 while the Start menu is open
#define LG2_RAM_PLAYER_PTR2 0x0300106C // another pointer to it that stays set in the Start menu
// The player object keeps its own copy of the current character's stats as whole
// numbers: three bytes (STR, POW, END) followed by two bytes that are normally 0x80.
// Combat and the status screen read this copy, not the character record.
#define LG2_PLAYER_STATS 0x160
// The first word of the player object (its class). LG2_RAM_PLAYER_PTR2 is NOT
// kept up to date: after an area change it can still hold the old player's
// address, which by then belongs to something else (an enemy, say). Check the
// class before writing through either pointer.
#define LG2_PLAYER_CLASS 0x08025AB4

static inline int lg2IsPlayer(uint32_t object) {
	return (object >> 24) == 2 && modRead32(object) == LG2_PLAYER_CLASS;
}

// --- How characters are drawn (worked out for the "more playable characters" project) ---
//
// Character slots: exactly 6, hard-coded (loops compare against 6; the records
// sit in a fixed array inside the CharacterData object with other fields after).
//
// LG2_ROM_FORMS: 22 records of 20 bytes, one per character form (base, Super
// Saiyan, ...). LG2Char.sprite is an index into it.
//   byte 0      flags (bit 0, bit 1 tested by code)
//   byte 1      sprite definition number -> LG2_ROM_SPRITE_DEFS (Gohan's forms 0-5
//               are definitions 0x35, 0x37, 0x38, 0x39, 0x3A, 0x3B; he starts the
//               game as form 3, an early outfit with only 18 animations)
//   byte 2, 3   text numbers (names), looked up by LG2_FN_GET_TEXT
//   word +16    address of a behaviour object (0x03001370..0x03001380, five
//               classes); its first virtual function sets up the player object
//
// LG2_ROM_SPRITE_DEFS: 157 pointers. A sprite definition is
//   u8 0, u8 directions (4), u8 animations, u8 0x10, u32 8, u32 0
//   directions x { s32 x, y, w, h }                 collision boxes
//   animations x directions pointers to frame lists
// A frame list is 12-byte frames: { s8 x, s8 y, u8 w, u8 h, u16 oam0, u16 oam1,
// pointer to graphics }. Graphics start { u32 type (1), u32 unpacked size }
// followed by data in a compression scheme not yet decoded (not BIOS LZ77).
//
// Buu's Fury has the same idea in a different layout: its sprite definitions
// start { 02 00 00 04, 0, 10 08 0a 04, 0 } before the collision boxes, so its
// sprites must be extracted and re-encoded, not copied.
// LG2_ROM_ATTACKS: 14 records of 16 bytes: two pointers (icon and name data),
// then "can it be fired" (character record) and "fire it" (character record,
// player) functions. Firing puts the player into a state that plays a fixed
// animation number, so a character's sprite must have that animation.
//   2 = energy blast (state 0x22)   3 = Kamehameha (state 0x30, animations 18+)
//   4 = Masenko (state 0x2D)
// Gohan: slot 0 Masenko, slot 1 Kamehameha, slot 3 energy blast.
#define LG2_ROM_ATTACKS 0x2A76C
#define LG2_ATTACK_BLAST 2
#define LG2_ATTACK_KAMEHAMEHA 3
#define LG2_ATTACK_MASENKO 4
#define LG2_ATTACK_OWNED(slot) (8u << (slot))
#define LG2_ROM_FORMS 0x2A4E8
#define LG2_ROM_SPRITE_DEFS 0x692E4C
#define LG2_FN_GET_SPRITE_DEF 0x080061BC   // def* (number); 0 = current player, 1-6 = character slots
#define LG2_FN_GET_TEXT 0x0801F360         // (number): NOT text, despite the old name; numbers over 7 index the portrait table
// The game's text: 20 bytes a line, a pointer for each of five languages,
// English first. Area names are in here (an area record's word at +12 is its
// name's number, and so is the word at +16 of a world map landing spot).
#define LG2_ROM_TEXT 0x6C9044

// Actor objects (the player and enemies share these):
//   +0x10 u8 facing direction      +0x12 u16 animation timer   +0x14 u16 frame number
//   +0x40 s32 screen x, +0x44 s32 screen y (sprite top-left)   +0x48 sprite definition
//   +0x4C current animation: sprite definition + 0x4C + 16 * animation number
// Starting an animation is just: timer = 0, frame = 0, byte +8 = 0x28, set +0x4C.
// Animation numbers differ per character (idle is 0 and walking 1 for everyone seen so far).
#define LG2_ACTOR_FRAME 0x14
#define LG2_ACTOR_FRAME_RECORD 0x20 // the 12-byte frame being shown: { s8 x, s8 y, u8 w, u8 h, ... }
#define LG2_ACTOR_SCREEN_X 0x40 // s32: where the sprite's top-left corner is drawn
#define LG2_ACTOR_SCREEN_Y 0x44
#define LG2_ACTOR_SPRITE_DEF 0x48
#define LG2_ACTOR_ANIMATION 0x4C
#define LG2_SPRITE_DEF_ANIMATIONS 0x4C

// The "Switch Characters" screen (opened from a save pad once two characters
// can be played). While it is open, this return address is on the game's stack.
// The screen draws each character as a sprite, left to right in character order,
// and the selection brackets as one more sprite at the same spot as the
// selected character.
// (0x08009EC3, used before, belongs to the main loop and is on the stack almost
// always, dialogues included; 0x0801EC97 is only there while this screen runs.)
#define LG2_SWITCH_SCREEN_RETURN 0x0801EC97
#define LG2_STACK_TOP 0x03007F00

// LG2_ROM_PORTRAITS: 107 pointers to 64x64 portraits (plain rows of pixels once
// unpacked, not tiles). Numbers 0-17 are the playable characters as shown on
// the status screen and use that screen's own background palette; the rest are
// dialogue portraits and use the fixed sprite palette. Status: Gohan 0-3 (one
// per outfit) and 4-5 (Super Saiyan), Goku 6 and 7 (Super Saiyan), Piccolo 8-9,
// Trunks 11-15, Vegeta 16 and 17 (Super Saiyan). Dialogue: Gohan 48, 50-52 and
// 53-54, Goku 55 and 57, Vegeta 98 and 99.
#define LG2_ROM_PORTRAITS 0x6A981C
#define LG2_FN_UNPACK 0x0801F844           // void (packed graphics, destination): every portrait and sprite frame goes through here

// Areas. The world object holds the zone and area being played; walking into
// an exit runs LG2_FN_TAKE_EXIT, which loads the area the exit record names.
// Exit record (24 bytes): +16 kind, +17 zone, +18 area, +19 flag, +20 s16 x, y
// to arrive at. The exit object handed to the routine: +16 kind, +17 mask,
// +20 pointer to the record. Zone 8 is the Northern Mountains; its areas 50
// and 51 are the egg-quest versions of 45 (the hatchery at the top) and 43
// (the foot of the mountain).
#define LG2_RAM_ZONE 0x03000F0C            // u8 zone, then u8 area
#define LG2_RAM_CAMERA_X 0x03000F3C        // world pixel at the left edge of the screen
#define LG2_RAM_CAMERA_Y 0x03000F40
#define LG2_ACTOR_WORLD_X 0x158            // 24.8 fixed point
#define LG2_ACTOR_WORLD_Y 0x15C
#define LG2_FN_TAKE_EXIT 0x0801EE5C        // int (exit object, player): only acts while the exit's direction is held
#define LG2_RAM_INPUT_PTR 0x0802E958       // ROM word pointing at the input record; u16 at +18 = buttons held
// Zone 6 area 99 is the world map. Its landing spots are 20-byte records
// starting 33 EF 01 08 63 F2 01 08 (then the rectangle); the Northern
// Mountains' is around 2608,1044 and the wasteland's (zone 7) around 1776,2364.
#define LG2_FN_LOAD_AREA 0x08004610        // (world, zone, area, s16 position[2], flag, 1)

// Functions (Thumb)
#define LG2_FN_GET_LEVEL 0x08008A94        // int (LG2Char*)
#define LG2_FN_XP_TO_NEXT 0x08008AB0       // int (LG2Char*), 0 at the level cap
#define LG2_FN_XP_INTO_LEVEL 0x08008ADA    // int (LG2Char*, level)
#define LG2_FN_XP_FOR_LEVEL 0x08008AEA     // int (level)
#define LG2_FN_SET_LEVEL_XP 0x08008AF6     // void (LG2Char*, level): xp = start of that level
#define LG2_FN_LEVEL_UP 0x08008C44         // void (chardata, character, oldLevel, newLevel)
#define LG2_FN_ADD_XP 0x08008CFC           // void (chardata, character, _, amount on stack)
#define LG2_FN_SAVE_CHARDATA 0x08008FD4    // called when the game writes its save: character data is stored from here
#define LG2_FN_PLAYER_TAKE_DAMAGE 0x08015844 // int (player, hit): hit+8 is the damage (s32); returns 1 if it killed
#define LG2_FN_PLAYER_ON_HIT 0x080158C6     // int (player, hit): everything that happens when the player is struck: takes the
                                           // damage, starts the flinch, and tells the story scripts "the player was hit"
                                           // (which is what cracks a carried egg, for one)
#define LG2_FN_PLAYER_FLINCH_BEGIN 0x08012434 // (state, player): starts the "just got hit" state: animation 2, attacks disabled
#define LG2_FN_ACTOR_TAKE_HIT 0x0801444E     // (actor, hit): shared hit reaction, called by the above
// Damage is worked out by the function at 0x0801452C (target, hit record). The
// shared hit reaction is then handed a pointer 16 bytes into that record:
//   -12 s32 scale (1024)      -8 attack descriptor      -4 attacker object
//    +0 critical flag          +4 damage before the attacker's multiplier
//    +8 final damage
// An attack descriptor starts { u8 type (0 = melee, uses STR; else energy, uses
// POW), s32 base power at +4 }. Energy blast has base power 1024 per shot;
// Kamehameha has 64 per tick of the beam, several ticks a second.
// damage = (128 - target END) * base / 128 * f(stat) / 2^18, slightly randomised,
// where f(s) = (26 s^2 - 701 s + 115618) * s / 256. It rounds down, so a weak
// character's Kamehameha does 0 per tick.
#define LG2_ROM_KAMEHAMEHA_HIT 0x08028F68 // the Kamehameha beam's attack descriptor
#define LG2_ROM_BLAST_HIT 0x08028F30      // the others, 8 bytes each: the plain energy blast,
#define LG2_ROM_BIG_BANG_HIT 0x08028F58   // Big Bang,
#define LG2_ROM_SPECIAL_BEAM_HIT 0x08028F70 // the Special Beam Cannon,
#define LG2_ROM_SPIRIT_BOMB_HIT 0x08028F88  // the Spirit Bomb
int lg2GrowingAttackTier(uint32_t descriptor); // gohan_attacks.c: 0, or 1 (level 25+) or 2 (50+) for the attacks that grow with level
// (the older note, kept for the layout of the tail:)
// A hit is { u32 0, s32 raw damage, s32 damage, attacker object, u32 kind }.
// Enemies: object+0x180 points at the enemy type ({ s32 experience reward, s32 max HP, ... }),
// object+0x188 is its current HP.
#define LG2_ENEMY_TYPE 0x180
#define LG2_ENEMY_HP 0x188
#define LG2_FN_ENEMY_TAKE_DAMAGE 0x08014E2C     // int (enemy, hit): 1 if it died
#define LG2_FN_ENEMY_TAKE_DAMAGE_HIT 0x08014E4A // inside it, just after the shared hit reaction: r5 = enemy, r6 = hit, and
                                                // the damage at hit+8 is about to come off the enemy's health
#define LG2_FN_ENEMY_KILLED 0x08014E7A       // void (enemy): runs once when an enemy dies; hands out its experience
#define LG2_ENEMY_KILLED_LEVEL_CHECK 0x08014EA0 // return address of its "is the player level 50" check
#define LG2_FN_RANDOM 0x080218CC           // int (n): 0 .. n-1
#define LG2_FN_PLAYER_REFRESH 0x08015786   // void (player): reload the player object from the current character

// Game address of a line of the game's text, in English
static inline uint32_t lg2Text(int number) {
	return *(const uint32_t*) (modRom(LG2_ROM_TEXT) + number * 20);
}

static inline struct LG2Char* lg2Char(int who) {
	return (struct LG2Char*) ((uint8_t*) modRam(LG2_RAM_CHARDATA) + 12) + who;
}

static inline int lg2CurrentChar(void) {
	return *(uint8_t*) modRam(LG2_RAM_CHARDATA);
}

// Stats go up to 200 (the game's own limit was 100; raised to match Buu's
// Fury). Endurance is the exception in a fight: the game's damage formula
// takes it away from 128, so a fighting endurance of 128 would mean no damage
// at all. Above 100 it therefore counts for less: 200 on the status screen
// fights as 124.
#define LG2_STAT_CAP 200
#define LG2_ROM_STATUS_STAT_TEXT 0xE7C8 // "%d/100": how the status screen writes a stat

static inline int lg2FightingEndurance(int endurance) {
	return endurance <= 100 ? endurance : 100 + (endurance - 100) * 24 / 100;
}

// Copy the current character's stats into the player object, as the game does
// itself after a level-up. Call this after changing stats in the record.
static inline void lg2SyncPlayerStats(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if (!player) {
		player = modRead32(LG2_RAM_PLAYER_PTR2);
	}
	uint8_t* copy = lg2IsPlayer(player) ? modRam(player + LG2_PLAYER_STATS) : 0;
	if (copy) {
		const struct LG2Char* c = lg2Char(lg2CurrentChar());
		int i;
		for (i = 0; i < LG2_STATS; ++i) {
			copy[i] = i == LG2_END ? lg2FightingEndurance(c->stat[i] >> 8) : c->stat[i] >> 8;
		}
	}
}

// The sprite definition number a form uses (byte 1 of its LG2_ROM_FORMS record)
static inline int lg2FormDef(int form) {
	return modRom(LG2_ROM_FORMS)[form * 20 + 1];
}

// Whether the Switch Characters screen is open
static inline int lg2SwitchScreenOpen(void) {
	uint32_t a;
	if (modRead32(LG2_RAM_PLAYER_PTR)) {
		return 0; // the screen replaces the world: no player object while it is up
	}
	for (a = modRegisters()[13] & ~3U; a >= 0x03007000 && a < LG2_STACK_TOP; a += 4) {
		if (modRead32(a) == LG2_SWITCH_SCREEN_RETURN) {
			return 1;
		}
	}
	return 0;
}

// Which figure the brackets are on, counting from the left (0 = first), or -1.
// Worked out from the sprites: the brackets are the sprite that shares its
// position with another one.
static inline int lg2SwitchScreenSlot(void) {
	int xs[16], n = 0, i, j, cursor = -1, slot = 0;
	for (i = 0; i < 16 && n < 16; ++i) {
		uint16_t a0 = modRead16(0x07000000 + i * 8);
		if ((a0 & 0x300) != 0x200) {
			xs[n++] = modRead16(0x07000002 + i * 8) & 0x1FF;
		}
	}
	for (i = 0; i < n; ++i) {
		for (j = i + 1; j < n; ++j) {
			if (xs[i] == xs[j]) {
				cursor = xs[i];
			}
		}
	}
	if (cursor < 0) {
		return -1;
	}
	for (i = 0; i < n; ++i) {
		int seen = 0;
		for (j = 0; j < i; ++j) {
			seen |= xs[j] == xs[i];
		}
		if (!seen && xs[i] < cursor) {
			++slot;
		}
	}
	return slot;
}

// The character the brackets are on (the slots are the playable characters in order), or -1
static inline int lg2SwitchScreenChar(void) {
	int slot = lg2SwitchScreenSlot(), who;
	if (slot < 0) {
		return -1;
	}
	for (who = 0; who < LG2_CHARACTERS; ++who) {
		if ((lg2Char(who)->flags & 1) && slot-- == 0) {
			return who;
		}
	}
	return -1;
}

// Every actor on screen that is drawn with sprite definition `from` (the
// player, a follower, the figures on the Switch Characters screen) is moved to
// `to`, keeping its place in whatever animation it is in. Used when a mod
// swaps one sprite for another in LG2_ROM_SPRITE_DEFS.
static inline void lg2MoveActors(uint32_t from, uint32_t to) {
	uint32_t* ram = modRam(0x02000000);
	uint32_t i;
	for (i = 0; i + 1 < 0x40000 / 4; ++i) {
		if (ram[i] == from && ram[i + 1] >= from && ram[i + 1] < from + LG2_SPRITE_DEF_ANIMATIONS + 32 * 16) {
			ram[i] = to;
			ram[i + 1] = ram[i + 1] - from + to;
		}
	}
}

// Which forms belong to whom (LG2Char.sprite values): Gohan 0-5 (4, 5 Super
// Saiyan), Piccolo 6-7, Vegeta 8-11 (10, 11 Super Saiyan), Trunks 12-18, Goku
// 19 and 20 (Super Saiyan), Hercule 21. Transforming changes LG2Char.sprite
// to the Super Saiyan form (Vegeta 8 -> 11) once the transform animation
// (animation 9 of the ordinary sprite) has played.
#define LG2_FORM_GOKU 19
#define LG2_FORM_GOKU_BASE 19
#define LG2_FORM_GOKU_SSJ 20
#define LG2_FORM_VEGETA 8
#define LG2_FORM_VEGETA_SSJ 10

// State other mods need to know about (looks.c picks portraits from these)
extern int lg2GohanLook;   // adult_gohan.c: 0 kid, 1 adult, 2 Great Saiyaman, 3 Saiyaman without the helmet
extern int lg2FusionKind;  // fusion.c: 0 not fused, 1 Gogeta, 2 Vegito, 3 Gotenks
extern int lg2TrunksLook;  // looks.c: 0 future Trunks, 1 kid Trunks
extern int lg2FusionHost;  // fusion.c: the character who is fused, or -1
int lg2FusionWaiting(void); // fusion.c: still in the wait after a fusion
void lg2FusionReady(void);  //           end that wait now

// Only one hook can sit on a game function. blocking.c owns the one on "the
// player takes damage"; other mods that need to see or change the damage add a
// filter, and they are called in the order added.
void lg2AddPlayerHitFilter(void (*fn)(int32_t* damage));

// hub.c: the "Extras" page of the Start menu, which is how every mod feature
// is reached (nothing has a keyboard key of its own). While it is up the game
// is not paused (its music keeps playing) but gets no buttons; a menu opened
// from it must neither pause the game nor unpause it.
extern int lg2HubOpen;
// Leave the Start menu and, once the character is back in the world, call `then`
void lg2HubCloseThen(void (*then)(void));
// The menus and actions the page offers
void lg2OpenShop(int page);          // shop.c: 0 equip, 1 buy gear, 2 techniques, 3 supplies
int lg2ShopIsOpen(void);
void lg2OpenStatMenu(void);          // progression.c
int lg2StatMenuIsOpen(void);
int lg2StatPointsWaiting(void);      // points unspent, all characters together
void lg2OpenTransmission(void);      // transmission.c
int lg2BossFightsHere(void);         // bosses.c: the Extras page offers BOSS FIGHT in this area
int lg2BossMenuIsOpen(void);
void lg2OpenBossMenu(void);
int lg2TransmissionIsOpen(void);
int lg2Ssj3Possible(void);           // looks.c: Goku is a Super Saiyan (or already Super Saiyan 3)
void lg2DrawSlotLetter(const char* const rows[6]); // looks.c: a letter over the attack box, in the Z icon's style
int lg2OnSavePad(void);              // savepad.c: the character stands on a save pad (a Capsule Corp circle)
int lg2Ssj3On(void);
int lg2LooksSceneBusy(void);         // looks.c: a transformation scene has the player on a sprite that is not his form's
void lg2DrawHalo(uint32_t actor);    // halo.c: the ring over an actor's head
uint32_t lg2BfAreaRecord(int zone, int area); // bf_areas.c: an imported area's record (game address), or 0
void lg2ToggleSsj3(void);
const char* lg2ToughnessName(void);  // combat.c
void lg2NextToughness(void);
extern int lg2Untouchable;           // blocking.c: enemies' attacks pass through the player
int lg2ThirdSpecial(int who);        // shop.c: the attack in the character's third special slot (a learned technique), or -1
const char* lg2ThirdSpecialName(int who);
int lg2StartMenuPage(void);
void lg2RepairRecords(void);         // shop.c: put each character's own specials back in his slots
void lg2AddPlace(const char* name, int zone, int area, int x, int y); // transmission.c: a place of a mod's own on the list
void lg2GoTo(int zone, int area, int x, int y); // transmission.c: take the character there now (he must be in the world)          // hub.c: -1 none, 0 status, 1 items, 2 options, 3 journal

// enemy_hits.c: for attacks the mods add
int lg2FindEnemies(uint32_t player, uint32_t* found, int most);
void lg2EnemyPlace(uint32_t enemy, int* x, int* y, int* width); // middle of its feet on screen, and its width
void lg2HitEnemy(uint32_t player, uint32_t enemy, int basePower); // an energy blast is 1024
void lg2RegisterSpriteDef(uint32_t def); // enemy_hits.c: an object drawn with this definition of a mod's own counts as an enemy
extern int lg2HookSeen; // set by combat.c's hook when the game's damage routine runs: the damage it saw

// combat.c: money, and how hard hits land (100 = as the game has it)
int lg2Zenie(void);
void lg2AddZenie(int amount);
extern int lg2DamageDealtPercent, lg2DamageTakenPercent, lg2ZeniePercent;
// shop.c writes the player's fighting stats every frame: the character's own,
// times this (looks.c raises it for Super Saiyan 3), plus equipment
extern int lg2StatBoostPercent;

// Levels, as changed by progression.c (the raised cap)
int lg2Level(const struct LG2Char* c);
uint32_t lg2XpForLevel(int level);
#define LG2_LEVEL_CAP 100

static inline const char* lg2Name(int who) {
	static const char* const names[LG2_CHARACTERS] = { "GOHAN", "PICCOLO", "VEGETA", "TRUNKS", "GOKU", "HERCULE" };
	return who >= 0 && who < LG2_CHARACTERS ? names[who] : "?";
}

#endif
