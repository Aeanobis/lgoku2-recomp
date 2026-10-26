// Boss fights: in Majin Buu's battleground from Buu's Fury (zone 39 here),
// the Start menu's Extras page offers BOSS FIGHT, a list of the game's own
// bosses; pick one and he appears beside you, fighting as he does in the story.
//
// An enemy is an object of its own: the game's constructor (0x08015127) builds
// it from a spawn record {ctor, condition, type index, x, y, sprite def
// number, defeat record, behaviour list} and the level's world list is told
// about it. The type (stats, AI, what happens on death) comes from the game's
// table of 109 enemy types; bosses there "die" by standing invulnerable for
// their cutscene, so each chosen boss gets a copy of his type in a spare slot
// with the ordinary death (an explosion, then gone) and some experience.
#include "lg2.h"

#include <stdio.h>
#include <string.h>

// Majin Buu's forms, carried over from Buu's Fury (tools/sprites/convert_bf_boss.py)
#include "generated/fatBuu_data.inc"
#include "generated/evilBuu_data.inc"
#include "generated/superBuu_data.inc"
#include "generated/kidBuu_data.inc"

enum { BTN_A = 1, BTN_B = 2, BTN_UP = 64, BTN_DOWN = 128 };
#define ARENA_ZONE 39

#define LG2_ROM_ENEMY_TYPES 0x3244C      // 109 x 24: s32 xp, s32 max HP, u32 AI, u8 STR, POW, END, 0x80, u32 0x80, death record
#define LG2_FN_ENEMY_CTOR 0x08015127     // (spawn record) -> enemy object
#define LG2_FN_WORLD_ADD 0x0801E0C9      // (world list, object)
#define LG2_RAM_WORLD_LIST 0x030013B0    // +4 count, 128 at most
#define DEATH_EXPLODE 0x0802F90C         // the death record that removes the body
#define BEHAVIOUR_IDLE 0x08003B4B
#define DEFEAT_NOTHING 0x0802696D        // a bare `bx lr`
#define SCRATCH 0x8F5000                 // spawn records, 0x40 each
#define SPAWN_SLOTS 8

// Sprite definition numbers past the game's table are the mod's own (see onGetDef)
enum { OWN_DEFS = 200, DEF_FAT_BUU = 200, DEF_EVIL_BUU, DEF_SUPER_BUU, DEF_KID_BUU };
static struct { uint32_t offset; } ownDefs[4];

// type: the game's enemy type to copy (stats, AI); where hp/str/pow/end/ai are
// given they replace the copy's (the Buus borrow Perfect Cell's record with the
// Cell Jr / Cooler fighting AI, 21, which needs no beam animations)
static const struct { const char* name; uint8_t type, def; int xp; int hp, str, pow, end, ai; } bosses[] = {
	{ "MECHA FRIEZA", 42, 48, 4000 },
	{ "KING COLD", 62, 74, 3000 },
	{ "ANDROID 19", 5, 12, 5000 },
	{ "ANDROID 20", 6, 13, 5000 },
	{ "ANDROID 18", 4, 11, 6000 },
	{ "ANDROID 17", 3, 9, 6000 },
	{ "ANDROID 16", 2, 8, 6000 },
	{ "IMPERFECT CELL", 60, 28, 8000 },
	{ "SEMI-PERFECT CELL", 76, 29, 9000 },
	{ "CELL JUNIOR", 26, 31, 5000 },
	{ "PERFECT CELL", 25, 30, 12000 },
	{ "COOLER", 29, 34, 12000 },
	{ "MAJIN BUU", 25, DEF_FAT_BUU, 10000, 9000, 55, 58, 60, 21 },
	{ "EVIL BUU", 25, DEF_EVIL_BUU, 11000, 9500, 60, 62, 58, 21 },
	{ "SUPER BUU", 25, DEF_SUPER_BUU, 14000, 11500, 68, 70, 66, 21 },
	{ "KID BUU", 25, DEF_KID_BUU, 16000, 13000, 76, 82, 70, 21 },
};
#define BOSSES ((int) (sizeof(bosses) / sizeof(bosses[0])))
// Type slots no area or journal entry uses: each spawn takes the next
static const uint8_t spareTypes[] = { 43, 49, 52, 82, 87, 93, 95, 96, 97 };
#define SPARES ((int) sizeof(spareTypes))

static int open, cursor, first, chosen = -1, spawns; // first: the first row shown (the list scrolls)
#define ROWS 10

// A boss's sprite must have every animation the fighting AI asks for (0
// idle, 1 walk, 2 flinch, 5 fire pose, 6-8 attacks, 12), or the AI reads
// past its rows and draws from a garbage pointer (King Cold's has five: in the
// story he never fights). A short definition gets a longer copy with the
// missing rows standing in: each facing's record is the idle pose's, padded
// to eight entries (an attack steps through up to seven).
#define DEF_COPIES_ROM 0x8F6000
#define DEF_COPY_SPACE 0x800
#define NEEDED_ROWS 13
#define RECORD_ENTRY 12
#define RECORD_ENTRIES 8
static int defCopies;

static int emptyRow(const uint8_t* row) {
	int i;
	for (i = 0; i < 16; ++i) {
		if (row[i] != 0 && row[i] != 0xFF) {
			return 0;
		}
	}
	return 1;
}

static void ensureAnimations(int defNumber) {
	uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
	uint8_t* def = modRom(table[defNumber] - 0x08000000);
	uint8_t* rows = def + LG2_SPRITE_DEF_ANIMATIONS;
	static const uint8_t wanted[] = { 2, 5, 6, 7, 8, 12 };
	int count = def[2], missing = 0, r, f, e;
	unsigned i;
	for (i = 0; i < sizeof(wanted); ++i) {
		missing |= wanted[i] >= count || emptyRow(rows + wanted[i] * 16);
	}
	if (!missing || count > NEEDED_ROWS) {
		return;
	}
	uint8_t* copy = modRom(DEF_COPIES_ROM + defCopies * DEF_COPY_SPACE);
	uint8_t* records = copy + LG2_SPRITE_DEF_ANIMATIONS + NEEDED_ROWS * 16;
	const uint32_t* idle = (const uint32_t*) rows;
	int n[4];
	memcpy(copy, def, LG2_SPRITE_DEF_ANIMATIONS + count * 16);
	memset(copy + LG2_SPRITE_DEF_ANIMATIONS + count * 16, 0, (NEEDED_ROWS - count) * 16);
	copy[2] = NEEDED_ROWS;
	for (f = 0; f < 4; ++f) { // how long the idle pose's records are
		n[f] = f < 3 ? (int) ((idle[f + 1] - idle[f]) / RECORD_ENTRY) : n[2];
		if (n[f] < 1 || n[f] > RECORD_ENTRIES) {
			n[f] = 1;
		}
	}
	for (f = 0; f < 4; ++f) { // one padded record per facing, shared by every stand-in row
		const uint8_t* from = modRom(idle[f] - 0x08000000);
		for (e = 0; e < RECORD_ENTRIES; ++e) {
			memcpy(records + (f * RECORD_ENTRIES + e) * RECORD_ENTRY, from + (e < n[f] ? e : n[f] - 1) * RECORD_ENTRY, RECORD_ENTRY);
		}
	}
	for (r = 0; r < NEEDED_ROWS; ++r) {
		uint8_t* row = copy + LG2_SPRITE_DEF_ANIMATIONS + r * 16;
		if (r >= count || emptyRow(row)) {
			for (f = 0; f < 4; ++f) {
				*(uint32_t*) (row + f * 4) = 0x08000000 + DEF_COPIES_ROM + defCopies * DEF_COPY_SPACE + (uint32_t) (records - copy) + f * RECORD_ENTRIES * RECORD_ENTRY;
			}
		}
	}
	table[defNumber] = 0x08000000 + DEF_COPIES_ROM + defCopies * DEF_COPY_SPACE;
	++defCopies;
	modLog("bosses: sprite %d had %d animations: a %d-row copy stands in", defNumber, count, NEEDED_ROWS);
}
static unsigned messageUntil;
static char message[40];

int lg2BossFightsHere(void) {
	return modRead8(LG2_RAM_ZONE) == ARENA_ZONE;
}

int lg2BossMenuIsOpen(void) {
	return open;
}

void lg2OpenBossMenu(void) {
	open = 1;
	cursor = first = 0;
}

static void spawn(int which) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	int slot = spawns % SPAWN_SLOTS, type = spareTypes[spawns % SPARES];
	uint8_t* s = modRom(SCRATCH + slot * 0x40);
	uint8_t* t = modRom(LG2_ROM_ENEMY_TYPES + type * 24);
	int x = ((int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8) + 56, y = (int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8;
	uint32_t enemy;
	if (!lg2IsPlayer(player) || modRead32(LG2_RAM_WORLD_LIST + 4) >= 120) {
		return;
	}
	// his type, with the ordinary death and experience worth having
	memcpy(t, modRom(LG2_ROM_ENEMY_TYPES + bosses[which].type * 24), 24);
	*(int32_t*) t = bosses[which].xp;
	*(uint32_t*) (t + 20) = DEATH_EXPLODE;
	if (bosses[which].hp) {
		*(int32_t*) (t + 4) = bosses[which].hp;
		*(uint32_t*) (t + 8) = (uint32_t) bosses[which].ai;
		t[12] = (uint8_t) bosses[which].str;
		t[13] = (uint8_t) bosses[which].pow;
		t[14] = (uint8_t) bosses[which].end;
	}
	// the spawn record and the two records it points at
	memset(s, 0, 0x40);
	*(uint32_t*) (s + 0x00) = BEHAVIOUR_IDLE; *(uint32_t*) (s + 0x04) = 0x7D00;
	*(uint32_t*) (s + 0x08) = DEFEAT_NOTHING; *(uint32_t*) (s + 0x0C) = 0;
	*(uint32_t*) (s + 0x10) = LG2_FN_ENEMY_CTOR;
	*(uint32_t*) (s + 0x14) = 0;
	*(uint32_t*) (s + 0x18) = (uint32_t) type;
	*(int16_t*) (s + 0x1C) = (int16_t) x; *(int16_t*) (s + 0x1E) = (int16_t) y;
	*(uint32_t*) (s + 0x20) = bosses[which].def;
	*(uint32_t*) (s + 0x24) = 0x08000000 + SCRATCH + slot * 0x40 + 0x08;
	*(uint32_t*) (s + 0x28) = 1;
	*(uint32_t*) (s + 0x2C) = 0x08000000 + SCRATCH + slot * 0x40;
	enemy = modCall(LG2_FN_ENEMY_CTOR, 0x08000000 + SCRATCH + slot * 0x40 + 0x10, 0, 0, 0);
	if (enemy) {
		modCall(LG2_FN_WORLD_ADD, LG2_RAM_WORLD_LIST, enemy, 0, 0);
	}
	++spawns;
	modLog("bosses: %s (type %d as slot %d, def %d) spawned at (%d, %d): object %08X, %u objects in the world",
	       bosses[which].name, bosses[which].type, type, bosses[which].def, x, y, enemy, modRead32(LG2_RAM_WORLD_LIST + 4));
	snprintf(message, sizeof(message), "%s!", bosses[which].name);
	messageUntil = modFrame() + 120;
}

static void spawnChosen(void) {
	if (chosen >= 0 && modCanCall()) {
		spawn(chosen);
	}
	chosen = -1;
}

static void frame(void) {
	static uint32_t lastKeys;
	static int fresh;
	uint32_t keys = modKeys(), pressed = keys & ~lastKeys;
	int i;
	lastKeys = keys;
	if (modFrame() < messageUntil) {
		modTextScale(2);
		modText(8, 30, 0xFF4040, "%s", message);
		modTextScale(1);
	}
	if (!open) {
		fresh = 1;
		return;
	}
	if (fresh) { // the press that opened the list is not a press in it
		fresh = 0;
		pressed = 0;
	}
	if (pressed & BTN_B) {
		open = 0;
		return;
	}
	if (pressed & BTN_UP) {
		cursor = (cursor + BOSSES - 1) % BOSSES;
	}
	if (pressed & BTN_DOWN) {
		cursor = (cursor + 1) % BOSSES;
	}
	if (cursor < first) {
		first = cursor;
	}
	if (cursor >= first + ROWS) {
		first = cursor - ROWS + 1;
	}
	if (pressed & BTN_A) {
		chosen = cursor;
		open = 0;
		lg2HubCloseThen(spawnChosen); // the Start menu has to be closed before anyone can appear
		return;
	}
	modRect(30, 10, 180, 140, 0x402010);
	modRect(32, 12, 176, 136, 0x000000);
	modTextScale(2);
	modText(38, 16, 0xFF6060, "BOSS FIGHT");
	modTextScale(1);
	modText(136, 20, 0x909090, "%2d / %d", cursor + 1, BOSSES);
	for (i = first; i < BOSSES && i < first + ROWS; ++i) {
		int y = 36 + (i - first) * 9;
		modText(38, y, i == cursor ? 0xFFFFFF : 0x909090, "%c%s", i == cursor ? '>' : ' ', bosses[i].name);
		modText(150, y, i == cursor ? 0xC0C0C0 : 0x606060, "HP %d", bosses[i].hp ? bosses[i].hp : (int) modRead32(LG2_ROM_ENEMY_TYPES + bosses[i].type * 24 + 4 + 0x08000000));
	}
	modText(38, 140, 0x909090, "A: FIGHT   B: BACK   %s%s", first > 0 ? "^" : " ", first + ROWS < BOSSES ? "v" : " ");
}

// The game asks for sprite definitions by number: 0 the player, 1-6 the
// character slots, 7+ its table. Numbers from OWN_DEFS are the mod's.
static int onGetDef(uint32_t* r) {
	if (r[0] >= OWN_DEFS && r[0] < OWN_DEFS + 4) {
		r[0] = 0x08000000 + ownDefs[r[0] - OWN_DEFS].offset;
		return MOD_RETURN;
	}
	return MOD_CONTINUE;
}

MOD_INIT(bosses) {
	int i;
	uint32_t* own = (uint32_t*) ownDefs;
	memcpy(modRom(fatBuuRomOffset), fatBuuData, fatBuuSize);
	memcpy(modRom(evilBuuRomOffset), evilBuuData, evilBuuSize);
	memcpy(modRom(superBuuRomOffset), superBuuData, superBuuSize);
	memcpy(modRom(kidBuuRomOffset), kidBuuData, kidBuuSize);
	own[0] = fatBuuRomOffset; own[1] = evilBuuRomOffset; own[2] = superBuuRomOffset; own[3] = kidBuuRomOffset;
	for (i = 0; i < 4; ++i) {
		lg2RegisterSpriteDef(0x08000000 + own[i]);
	}
	for (i = 0; i < BOSSES; ++i) {
		if (bosses[i].def < OWN_DEFS) {
			ensureAnimations(bosses[i].def);
		}
	}
	modHook(LG2_FN_GET_SPRITE_DEF, onGetDef);
	modOnFrame(frame);
}
