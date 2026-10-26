// The areas (maps) of Buu's Fury, reached by Instant Transmission and then
// by walking: every area that can be carried over is here, with its exits.
//
// Both games run the same map engine, so a Buu's Fury map is carried over as
// it is (tools/maps/convert_bf_area.py: the header, layers, chunks, collision
// and tile list verbatim, pointers relocated, exit classes rewritten, the
// tile banks shared by every area in one copy). An imported area is empty:
// no enemies, people or scripts. Its exits lead where they did in Buu's Fury
// (zone z there is zone 20+z here), so the areas join up as they do in that
// game; a door into one of the few maps that cannot be carried over (a layer
// kind LoG2 has not got) leads back to where the list starts.
//
// Two hooks make it work: the game's area lookup (zone, area -> record) is
// answered for the imported zones, so nothing in the game's own table moves;
// and the routine that turns the game's global colour table into the
// background palette uses Buu's Fury's colour table instead while an imported
// area is the current one (each game has one such table, and they differ).
#include "lg2.h"

#include <string.h>

#include "generated/bf_banks_data.inc"
#include "generated/bf_areas_data.inc"
#include "generated/bf_palette.inc"

#define LG2_FN_FIND_AREA 0x080042F8      // (zone, area) -> area record, or 0
#define LG2_FN_LOAD_PALETTE 0x08009ED8   // (tint r, g, b, mix 0-256): the colour table -> the 512 colours
#define LG2_RAM_AREA_RECORD 0x03000F10   // the current area's record
#define LG2_ROM_COLOURS 0x2A9E8          // the game's colour table: RGB888 x 512 (256 background, 256 sprite)
#define BG_PALETTE 0x05000000
#define FIRST_IMPORTED_ZONE 21

#define AREAS ((int) (sizeof(bfAreaIndex) / sizeof(bfAreaIndex[0])))
#define PLACES ((int) (sizeof(bfPlaces) / sizeof(bfPlaces[0])))

// What the zones are (Buu's Fury's numbering). A zone without a line here
// is listed as "BF ZONE n"; one with no name is kept off the list (its areas
// are still there, through the exits from the others).
static const struct { uint8_t zone; const char* name; } zoneNames[] = {
	{ 1, 0 },                       // the tournament grounds, empty
	{ 2, 0 },                       // Goku's house
	{ 3, "NEW WEST CITY" },         // West City as it is in Buu's Fury
	{ 4, 0 },
	{ 5, 0 },                       // Kame House
	{ 6, 0 },                       // the crater where Vegeta blew himself up
	{ 7, 0 },                       // the martial arts tournament
	{ 8, 0 },                       // Buu's Fury's own menu screens (not a place: kept out altogether, below)
	{ 9, 0 },                       // a lighthouse
	{ 10, 0 },                      // Pilaf's castle, maybe
	{ 11, 0 },                      // the desert of the Dragon Ball hunt
	{ 12, 0 },
	{ 13, 0 },                      // a ninja village
	{ 14, 0 },
	{ 15, 0 },
	{ 18, 0 },
	{ 19, "MAJIN BUU'S BATTLEGROUND" },
	{ 20, 0 },
	{ 21, 0 },
	{ 22, "HEAVEN" },
};

// Zones that are not places at all: a lookup of one lands at the first place instead
static const uint8_t forbiddenZones[] = { 8 };
#define ZONE_NAMES ((int) (sizeof(zoneNames) / sizeof(zoneNames[0])))

static const char* zoneName(int zone, const char* fallback) {
	int i;
	for (i = 0; i < ZONE_NAMES; ++i) {
		if (zoneNames[i].zone == zone) {
			return zoneNames[i].name;
		}
	}
	return fallback;
}

static int find(int zone, int area) {
	int i;
	for (i = 0; i < AREAS; ++i) {
		if (bfAreaIndex[i].zone == zone && bfAreaIndex[i].area == area) {
			return i;
		}
	}
	return -1;
}

// For other mods: an imported area's record (game address), or 0
uint32_t lg2BfAreaRecord(int zone, int area) {
	int i = find(zone, area);
	return i < 0 ? 0 : bfAreaIndex[i].record;
}

static int imported(void) {
	uint32_t rec = modRead32(LG2_RAM_AREA_RECORD);
	return modRead8(LG2_RAM_ZONE) >= FIRST_IMPORTED_ZONE || (rec >= 0x08000000 + bfAreasRomOffset && rec < 0x08000000 + bfAreasRomOffset + bfAreasSize);
}

static int onFindArea(uint32_t* r) {
	int zone = (int) r[0], area = (int) r[1], i;
	if (zone < FIRST_IMPORTED_ZONE) {
		return MOD_CONTINUE;
	}
	i = find(zone, area);
	{
		unsigned k;
		for (k = 0; k < sizeof(forbiddenZones); ++k) {
			if (zone - 20 == forbiddenZones[k]) {
				i = -1;
			}
		}
	}
	if (i < 0) {
		modLog("bf_areas: no area %d:%d (Buu's Fury's %d:%d could not be carried over): the first place instead", zone, area, zone - 20, area);
		i = find(bfPlaces[0].zone, bfPlaces[0].area);
	}
	modLog("bf_areas: area %d:%d: Buu's Fury's %d:%d", zone, area, zone - 20, area);
	r[0] = bfAreaIndex[i].record;
	return MOD_RETURN;
}

// The game's own routine, with the other table for the backgrounds: each of
// the 512 colours (256 background, then 256 sprite: the sprites keep the
// game's own) mixed towards the tint (r0, r1, r2) by mix/256, as 15-bit
static int onLoadPalette(uint32_t* r) {
	int mix = (int) r[3], keep, i;
	if (!imported()) {
		return MOD_CONTINUE;
	}
	mix = mix > 256 ? 256 : mix < 0 ? 0 : mix;
	keep = 256 - mix;
	for (i = 0; i < 512; ++i) {
		const uint8_t* c = i < 256 ? bfPalette + i * 3 : (const uint8_t*) modRom(LG2_ROM_COLOURS) + i * 3;
		int red = (c[0] * keep + (int) r[0] * mix) >> 11;
		int green = (c[1] * keep + (int) r[1] * mix) >> 11;
		int blue = (c[2] * keep + (int) r[2] * mix) >> 11;
		modWrite16(BG_PALETTE + i * 2, (uint16_t) (red | (green << 5) | (blue << 10)));
	}
	return MOD_RETURN;
}

// A save state or the resume made with an earlier build keeps pointers into
// where the areas were laid out then; a character loaded inside an imported
// area whose record is not where the game thinks it is goes there afresh
static int recheck;

static void onStateRead(void) {
	recheck = 1;
}

static void frame(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	int zone = modRead8(LG2_RAM_ZONE), area = modRead8(LG2_RAM_ZONE + 1), i;
	if (!recheck || !lg2IsPlayer(player) || !modCanCall() || modPaused()) {
		return;
	}
	recheck = 0;
	if (zone < FIRST_IMPORTED_ZONE) {
		return;
	}
	i = find(zone, area);
	if (i >= 0 && modRead32(LG2_RAM_AREA_RECORD) != bfAreaIndex[i].record) {
		int x = (int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8, y = (int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8;
		modLog("bf_areas: area %d:%d was laid out elsewhere when this state was made: entering it again at (%d, %d)", zone, area, x, y);
		lg2GoTo(zone, area, x, y);
	}
}

MOD_INIT(bf_areas) {
	int i;
	recheck = 1;
	modOnStateRead(onStateRead);
	modOnFrame(frame);
	memcpy(modRom(bfBanksRomOffset), bfBanksData, bfBanksSize);
	memcpy(modRom(bfAreasRomOffset), bfAreasData, bfAreasSize);
	modHook(LG2_FN_FIND_AREA, onFindArea);
	modHook(LG2_FN_LOAD_PALETTE, onLoadPalette);
	for (i = 0; i < PLACES; ++i) {
		const char* name = zoneName(bfPlaces[i].zone - 20, bfPlaces[i].name);
		if (name) {
			lg2AddPlace(name, bfPlaces[i].zone, bfPlaces[i].area, bfPlaces[i].x, bfPlaces[i].y);
		}
	}
}
