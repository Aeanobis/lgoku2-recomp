// Heaven (Buu's Fury's, carried over by bf_areas.c: zone 42 area 1, reached by
// Instant Transmission): Goku is there in his halo, walking about, and has a
// few words for whoever comes up to him and presses A. He keeps to the open
// grass by the pillars.
//
// He is a person of the area the game's own way. An area record keeps its
// people at +0x2C, a list {map, then per person: behaviour {fn, arg}, object
// record {ctor, 0, x | y << 16, sprite def, 1, -> behaviour}}, and at +0x14 a
// table of pointers to the object records, +4 their count; the loader builds
// each person from its record (the standing behaviour 0x08003B4B, whose arg's
// third byte is the way he faces). The imported area has none, so it gets a
// list of its own with Goku in it: the game draws him, sorts him with the
// rest and keeps him out of the player's way. His walking is done here (the
// standing behaviour does not move him): his place, facing, animation and
// frame are set each frame. A near him never reaches the game, whose own
// talk would run the area's scripts, which Heaven has not got; the talk is
// drawn here instead.
#include "lg2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEAVEN_ZONE 42
#define HEAVEN_AREA 1
#define PEOPLE_ROM 0x8F4400        // free: the area's new people list, then the table
#define PERSON_CTOR 0x08015E83
#define PERSON_STAND 0x08003B4B
#define PERSON_CLASS 0x08025C74
#define GOKU_DEF 60                // Goku's sprite (his base form)
#define HOME_X 191
#define HOME_Y 171
// His patch of the field: the open grass between the save pad, the tree to
// the north-east and the two pillars (the left one from (165, 199) down, the
// right one from (227, 176) down)
#define PATCH_X0 172
#define PATCH_X1 205
#define PATCH_Y0 150
#define PATCH_Y1 192
#define TALK_RANGE 26
#define HUSH_RANGE 44              // within this, A never reaches the game
#define WALK_ANIMATION 1
#define STAND_ANIMATION 0
#define WALK_FRAMES 4
#define FRAME_TICKS 12             // the game walks its people at 2 pixels a frame with 8 ticks a frame; his stroll is a pixel a frame

enum { BTN_A = 1 };
enum { UP, DOWN, LEFT, RIGHT };   // the game's facings

static uint32_t goku;              // his person object, or 0
static int entered;                // the area entered again since the start (its people are built at load)
static int restFor, walkFor, facing = DOWN, tick;
static int talking, page;
static uint32_t lastKeys;

static uint32_t table(int def) {
	return ((const uint32_t*) modRom(LG2_ROM_SPRITE_DEFS))[def];
}

static void install(void) {
	uint32_t rec = lg2BfAreaRecord(HEAVEN_ZONE, HEAVEN_AREA);
	uint32_t* m = (uint32_t*) modRom(PEOPLE_ROM);
	uint8_t* r;
	if (!rec) {
		modLog("heaven: no area %d:%d (Buu's Fury's areas are not here): Goku stays away", HEAVEN_ZONE, HEAVEN_AREA);
		return;
	}
	r = modRom(rec - 0x08000000);
	m[0] = modRead32(*(uint32_t*) (r + 0x2C)); // the area's map, as the list's first word
	m[1] = PERSON_STAND;
	m[2] = 0x00007D00 | (DOWN << 16);
	m[3] = PERSON_CTOR;
	m[4] = 0;
	m[5] = HOME_X | (HOME_Y << 16);
	m[6] = GOKU_DEF;
	m[7] = 1;
	m[8] = 0x08000000 + PEOPLE_ROM + 4;  // -> his behaviour
	m[9] = 0x08000000 + PEOPLE_ROM + 12; // the table: -> his object record
	r[4] = 1;
	*(uint32_t*) (r + 0x14) = 0x08000000 + PEOPLE_ROM + 36;
	*(uint32_t*) (r + 0x2C) = 0x08000000 + PEOPLE_ROM;
}

static int inHeaven(void) {
	return modRead8(LG2_RAM_ZONE) == HEAVEN_ZONE && modRead8(LG2_RAM_ZONE + 1) == HEAVEN_AREA;
}

static uint32_t find(void) {
	const uint32_t* ram = modRam(0x02000000);
	uint32_t def = table(GOKU_DEF), i;
	for (i = 0; i + 0x60 < 0x40000 / 4; ++i) {
		if (ram[i] == PERSON_CLASS && ram[i + 0x48 / 4] == def) {
			return 0x02000000 + i * 4;
		}
	}
	return 0;
}

static int gokuX(void) { return (int) modRead32(goku + LG2_ACTOR_WORLD_X) >> 8; }
static int gokuY(void) { return (int) modRead32(goku + LG2_ACTOR_WORLD_Y) >> 8; }

// His animation, facing and frame: the sprite definition's animation entries
// are four pointers, one per facing, to lists of 12-byte frames
static void show(int animation, int frame) {
	uint32_t def = modRead32(goku + LG2_ACTOR_SPRITE_DEF);
	uint32_t entry = def + LG2_SPRITE_DEF_ANIMATIONS + 16 * animation;
	uint32_t list = modRead32(entry + 4 * facing);
	modWrite8(goku + 0x10, (uint8_t) facing);
	modWrite32(goku + LG2_ACTOR_ANIMATION, entry);
	modWrite16(goku + 0x14, (uint16_t) frame);
	modWrite32(goku + LG2_ACTOR_FRAME_RECORD, list + 12 * frame);
}

static void wander(void) {
	int x = gokuX(), y = gokuY();
	if (restFor > 0) {
		--restFor;
		show(STAND_ANIMATION, 0);
		return;
	}
	if (walkFor <= 0) {
		if ((modRandom(1000) + modFrame()) % 5 < 2) {
			restFor = 40 + (int) modRandom(80);
		} else {
			// a way with some room in it
			int way;
			for (way = (int) modRandom(4); ; way = (way + 1) % 4) {
				if ((way == LEFT && x > PATCH_X0 + 12) || (way == RIGHT && x < PATCH_X1 - 12) || (way == UP && y > PATCH_Y0 + 12) || (way == DOWN && y < PATCH_Y1 - 12)) {
					break;
				}
			}
			facing = way;
			walkFor = 20 + (int) modRandom(30);
			tick = 0;
			if (getenv("LG2_DEBUG")) {
				fprintf(stderr, "heaven f%u: off %d from (%d,%d) for %d\n", modFrame(), facing, x, y, walkFor);
			}
		}
		return;
	}
	--walkFor;
	++tick;
	if (getenv("LG2_DEBUG") && (tick & 3) == 1) {
		fprintf(stderr, "heaven f%u: walking %d, at (%d,%d), walkFor %d, object +0x158 %08X +0x15C %08X\n", modFrame(), facing, x, y, walkFor, modRead32(goku + LG2_ACTOR_WORLD_X), modRead32(goku + LG2_ACTOR_WORLD_Y));
	}
	{
		// A step past the edge of his patch stops him (he then chooses afresh);
		// a step back towards it, should the game have pushed him out, is fine
		int nx = x + (facing == LEFT ? -1 : facing == RIGHT ? 1 : 0), ny = y + (facing == UP ? -1 : facing == DOWN ? 1 : 0);
		int outNow = x < PATCH_X0 || x > PATCH_X1 || y < PATCH_Y0 || y > PATCH_Y1;
		int outNext = nx < PATCH_X0 || nx > PATCH_X1 || ny < PATCH_Y0 || ny > PATCH_Y1;
		int closer = abs(nx - HOME_X) + abs(ny - HOME_Y) < abs(x - HOME_X) + abs(y - HOME_Y);
		if (outNext && !(outNow && closer)) {
			walkFor = 0;
			restFor = 30 + (int) modRandom(60);
			show(STAND_ANIMATION, 0);
			if (getenv("LG2_DEBUG")) {
				fprintf(stderr, "heaven f%u: edge at (%d,%d) going %d\n", modFrame(), x, y, facing);
			}
			return;
		}
		x = nx;
		y = ny;
	}
	modWrite32(goku + LG2_ACTOR_WORLD_X, (uint32_t) (x << 8));
	modWrite32(goku + LG2_ACTOR_WORLD_Y, (uint32_t) (y << 8));
	show(WALK_ANIMATION, (tick / FRAME_TICKS) % WALK_FRAMES);
}

// ---- the talk ---------------------------------------------------------------------
#define LINES 2
static const char* const gokuLines[][LINES] = {
	{ "%s! HEY! YOU CAME ALL THE WAY UP HERE?", "DON'T TELL ME YOU DIED TOO... NO? GOOD, HEH." },
	{ "DON'T WORRY ABOUT ME. KING KAI'S PLACE IS GREAT,", "AND I'M EATING BETTER THAN EVER!" },
	{ "YOU ALL DID GREAT AGAINST CELL. I KNEW THE EARTH", "WAS IN GOOD HANDS." },
	{ "LOOK AFTER CHI-CHI AND THE OTHERS FOR ME. I'LL", "SEE YOU AGAIN SOMEDAY... MAYBE SOONER THAN YOU THINK!" },
};
static const char* const gokuToGoku[][LINES] = {
	{ "HUH? ANOTHER ME?", "..." },
	{ "KING KAI'S NEVER GONNA BELIEVE THIS ONE.", "WANNA SPAR?" },
};
#define PAGES ((int) (sizeof(gokuLines) / sizeof(gokuLines[0])))
#define PAGES_TO_GOKU ((int) (sizeof(gokuToGoku) / sizeof(gokuToGoku[0])))

static int near(uint32_t player, int range) {
	int dx = ((int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8) - gokuX();
	int dy = ((int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8) - gokuY();
	return dx > -range && dx < range && dy > -range && dy < range;
}

static void faceEachOther(uint32_t player) {
	int dx = ((int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8) - gokuX();
	int dy = ((int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8) - gokuY();
	if (dx * dx > dy * dy) {
		facing = dx > 0 ? RIGHT : LEFT;
	} else {
		facing = dy > 0 ? DOWN : UP;
	}
}

static void drawTalk(void) {
	int who = lg2CurrentChar(), toGoku = who == LG2_GOKU, i;
	const char* const* lines = toGoku ? gokuToGoku[page] : gokuLines[page];
	modRect(6, 112, 228, 42, 0x301810); // (colours are blue, green, red bytes)
	modRect(8, 114, 224, 38, 0x583828);
	modTextPlain(1);
	modText(14, 118, 0x40E0FF, "GOKU");
	for (i = 0; i < LINES; ++i) {
		modText(14, 128 + i * 9, 0xFFFFFF, lines[i], lg2Name(who));
	}
	modText(208, 144, 0xA0A0A0, "A");
	modTextPlain(0);
}

// While they talk the game runs on (the music, the field, his and the
// player's idle animations) but gets no buttons: the player stands and listens
static uint32_t filter(uint32_t keys) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if (talking) {
		return 0;
	}
	if (goku && inHeaven() && lg2IsPlayer(player) && near(player, HUSH_RANGE)) {
		keys &= ~(uint32_t) BTN_A;
	}
	return keys;
}

static void frame(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	uint32_t keys = modKeys(), pressed = keys & ~lastKeys;
	lastKeys = keys;
	if (!inHeaven()) {
		goku = 0;
		talking = 0;
		return;
	}
	if (!lg2IsPlayer(player)) {
		goku = 0;
		return;
	}
	if (!entered && modCanCall() && !modPaused() && !lg2HubOpen) {
		entered = 1;
		if (!find()) {
			lg2GoTo(HEAVEN_ZONE, HEAVEN_AREA, (int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8, (int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8);
			modLog("heaven: entered again, so that Goku is built");
			return;
		}
	}
	if (goku && modRead32(goku) != PERSON_CLASS) {
		goku = 0;
	}
	if (!goku) {
		goku = find();
		if (!goku) {
			return;
		}
		restFor = 60;
		modLog("heaven: Goku is here, object %08X at (%d, %d)", goku, gokuX(), gokuY());
	}
	if (talking) {
		faceEachOther(player);
		show(STAND_ANIMATION, 0);
		lg2DrawHalo(goku);
		drawTalk();
		if (pressed & BTN_A) {
			if (++page >= (lg2CurrentChar() == LG2_GOKU ? PAGES_TO_GOKU : PAGES)) {
				talking = 0;
			}
		}
		return;
	}
	if (modPaused()) {
		return;
	}
	if (getenv("LG2_DEBUG") && ((pressed & BTN_A) || modFrame() % 60 == 0)) {
		fprintf(stderr, "heaven f%u: %s player (%d,%d) goku (%d,%d) near %d canCall %d hub %d\n", modFrame(), (pressed & BTN_A) ? "A pressed:" : "",
		        (int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8, (int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8, gokuX(), gokuY(), near(player, TALK_RANGE), modCanCall(), lg2HubOpen);
	}
	if ((pressed & BTN_A) && near(player, TALK_RANGE) && modCanCall() && !lg2HubOpen && !lg2FusionKind) {
		talking = 1;
		page = 0;
		walkFor = 0;
		restFor = 30;
		faceEachOther(player);
		show(STAND_ANIMATION, 0);
		drawTalk();
		lg2DrawHalo(goku);
		return;
	}
	if (near(player, TALK_RANGE + 10)) {
		// Someone has come up to him: he stops and turns to them, as the game's
		// people do, so that A finds him facing
		walkFor = 0;
		if (restFor < 20) {
			restFor = 20;
		}
		faceEachOther(player);
	}
	wander();
	lg2DrawHalo(goku);
}

MOD_INIT(heaven) {
	install();
	modFilterKeys(filter);
	modOnFrame(frame);
}
