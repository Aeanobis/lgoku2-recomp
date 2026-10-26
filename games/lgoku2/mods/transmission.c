// Instant Transmission: the Start menu's Extras page (hub.c) opens a list of
// places; pick one with A and you are there.
//
// The list is the world map's own: each place the map would let you land at
// right now (the game's own condition on each landing spot is asked), by the
// name the map shows for it. So it never offers somewhere the story has not
// opened yet. You arrive where landing from the map would put you.
#include "lg2.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { BTN_A = 1, BTN_B = 2, BTN_UP = 64, BTN_DOWN = 128 };
enum { WORLD_MAP_ZONE = 6, WORLD_MAP_AREA = 99 };

#define LG2_ROM_AREAS 0x4EB80              // 56-byte records: u8 zone, u8 area, ... u8 triggers at +3, trigger list at +16
#define LG2_AREA_COUNT 328
#define LG2_FN_CONDITION 0x08008EAA        // int (condition): is this trigger switched on at the moment
#define LANDING_SPOT_CLASS 0x0801EF33      // first word of a world map landing spot record; its name's text number is at +16

// Where each landing spot leads, by the text number of its name: found by
// landing on each in turn
static const struct Place { uint16_t text; uint8_t zone, area; int16_t x, y; } places[] = {
	{ 203, 2, 6, 272, 240 }, { 223, 3, 1, 368, 360 }, { 253, 4, 3, 352, 1248 }, { 212, 9, 1, 768, 832 }, { 199, 9, 1, 768, 832 },
	{ 222, 8, 1, 272, 128 }, { 217, 11, 1, 416, 160 }, { 187, 5, 1, 360, 136 }, { 234, 7, 1, 208, 368 }, { 190, 6, 1, 248, 232 },
};
#define PLACES ((int) (sizeof(places) / sizeof(places[0])))

// Places other mods add (areas of their own), after the map's
#define EXTRA_PLACES 24
static struct { struct Place at; char name[36]; } extras[EXTRA_PLACES];
static int nExtras;

void lg2AddPlace(const char* name, int zone, int area, int x, int y) {
	if (nExtras < EXTRA_PLACES) {
		extras[nExtras].at.zone = (uint8_t) zone;
		extras[nExtras].at.area = (uint8_t) area;
		extras[nExtras].at.x = (int16_t) x;
		extras[nExtras].at.y = (int16_t) y;
		snprintf(extras[nExtras].name, sizeof(extras[nExtras].name), "%s", name);
		++nExtras;
	}
}

#define EXIT_ROM 0x8F2000 // a made-up exit, in the unused top of the ROM

static int open, cursor, count, leaving, first; // first: the first row shown (the list scrolls)
#define ROWS 9
static struct { struct Place at; char name[36]; } list[PLACES + EXTRA_PLACES];

// The world map's landing spots that are switched on, with their names
static void buildList(void) {
	const uint8_t* areas = modRom(LG2_ROM_AREAS);
	int i, k, p;
	count = 0;
	for (i = 0; i < LG2_AREA_COUNT; ++i) {
		const uint8_t* area = areas + i * 56;
		if (area[0] != WORLD_MAP_ZONE || area[1] != WORLD_MAP_AREA) {
			continue;
		}
		const uint32_t* triggers = (const uint32_t*) modRom((*(const uint32_t*) (area + 16)) & 0xFFFFFF);
		for (k = 0; k < area[3] && count < PLACES; ++k) {
			const uint32_t* record = (const uint32_t*) modRom(triggers[k * 2 + 1] & 0xFFFFFF);
			if (record[0] != LANDING_SPOT_CLASS) {
				continue;
			}
			for (p = 0; p < PLACES; ++p) {
				if (places[p].text == record[4]) {
					break;
				}
			}
			if (p == PLACES || (triggers[k * 2] && !modCall(LG2_FN_CONDITION | 1, triggers[k * 2], 0, 0, 0))) {
				continue; // somewhere not worked out, or not open at the moment
			}
			int seen = 0, j;
			for (j = 0; j < count; ++j) {
				seen |= list[j].at.zone == places[p].zone;
			}
			if (seen) {
				continue;
			}
			uint32_t text = lg2Text(places[p].text);
			char* name = list[count].name;
			for (j = 0; j < 35; ++j) {
				int c = (text >> 24) == 8 ? modRead8(text + j) : 0;
				if (c < 32 || c > 126) {
					break;
				}
				name[j] = (char) toupper(c);
			}
			name[j] = '\0';
			if (!j) {
				strcpy(name, "SOMEWHERE");
			}
			list[count++].at = places[p];
		}
	}
	for (i = 0; i < nExtras && count < PLACES + EXTRA_PLACES; ++i) {
		list[count].at = extras[i].at;
		strcpy(list[count].name, extras[i].name);
		++count;
	}
}

static uint32_t filter(uint32_t keys) {
	return leaving ? 0 : keys;
}

static void go(const struct Place* to) {
	uint8_t* exit = modRom(EXIT_ROM);
	uint8_t* record = exit + 32;
	memset(exit, 0, 64);
	exit[17] = 0xFF;
	*(uint32_t*) (exit + 20) = 0x08000000 + EXIT_ROM + 32;
	record[17] = to->zone;
	record[18] = to->area;
	record[19] = 1;
	*(int16_t*) (record + 20) = to->x;
	*(int16_t*) (record + 22) = to->y;
	// as in flight_circle.c: the exit routine wants a direction held
	uint32_t held = modRead32(LG2_RAM_INPUT_PTR) + 18;
	uint16_t was = modRead16(held);
	modWrite16(held, 0xFF);
	modCall(LG2_FN_TAKE_EXIT | 1, 0x08000000 + EXIT_ROM, modRead32(LG2_RAM_PLAYER_PTR), 0, 0);
	modWrite16(held, was);
}

// Straight there, for other mods (the character must be standing in the world)
void lg2GoTo(int zone, int area, int x, int y) {
	struct Place to = { 0, (uint8_t) zone, (uint8_t) area, (int16_t) x, (int16_t) y };
	go(&to);
}

static int fresh;

static void startLeaving(void) {
	modLog("transmission: leaving in a moment");
	leaving = 6;
}

// Opened from the Start menu's Extras page
void lg2OpenTransmission(void) {
	if (modCanCall()) {
		buildList();
		modLog("transmission: list open, %d places", count);
		open = fresh = 1;
		cursor = first = 0;
		if (!lg2HubOpen) {
			modSetPaused(1);
		}
	}
}

int lg2TransmissionIsOpen(void) {
	return open;
}

static void frame(void) {
	static uint32_t lastKeys;
	static unsigned noteUntil;
	static int target;
	uint32_t keys = modKeys(), pressed = keys & ~lastKeys;
	int playing = (modRead32(LG2_RAM_PLAYER_PTR) >> 24) == 2;
	int i;
	lastKeys = keys;
	if (leaving) {
		// the character has been given a few frames to come to a stop; now go
		if (!playing) {
			modLog("transmission: not in the world after all: staying");
			leaving = 0;
		} else if (--leaving <= 0) {
			if (modCanCall()) {
				leaving = 0;
				modLog("transmission: taking the exit");
				go(&list[target].at);
			} else {
				leaving = 1;
			}
		}
		return;
	}
	(void) noteUntil;
	if (!open) {
		return;
	}
	if (fresh) { // the press that opened it is not a press in it
		fresh = 0;
		pressed = 0;
	}
	if (pressed & BTN_B) {
		modLog("transmission: list closed");
		open = 0;
		if (!lg2HubOpen) {
			modSetPaused(0);
		}
		return;
	}
	if (count && (pressed & BTN_UP)) {
		cursor = (cursor + count - 1) % count;
	}
	if (count && (pressed & BTN_DOWN)) {
		cursor = (cursor + 1) % count;
	}
	if (cursor < first) {
		first = cursor;
	}
	if (cursor >= first + ROWS) {
		first = cursor - ROWS + 1;
	}
	if (count && (pressed & BTN_A)) {
		target = cursor;
		modLog("transmission: going to %s", list[cursor].name);
		open = 0;
		lg2HubCloseThen(startLeaving); // the Start menu has to be closed before he can go anywhere
		return;
	}
	modRect(30, 14, 180, 132, 0x402010);
	modRect(32, 16, 176, 128, 0x000000);
	modTextScale(2);
	modText(38, 20, 0x40C0FF, "INSTANT");
	modText(38, 32, 0x40C0FF, "TRANSMISSION");
	modTextScale(1);
	if (!count) {
		modText(38, 54, 0xC0C0C0, "NOWHERE TO GO YET.");
	}
	for (i = first; i < count && i < first + ROWS; ++i) {
		modText(38, 50 + (i - first) * 9, i == cursor ? 0xFFFFFF : 0x909090, "%c%s", i == cursor ? '>' : ' ', list[i].name);
	}
	if (count > ROWS) {
		modText(170, 134, 0x909090, "%s%s", first > 0 ? "^" : " ", first + ROWS < count ? "v" : " ");
	}
	modText(38, 134, 0x909090, "A: GO THERE   B: STAY");
}

MOD_INIT(transmission) {
	modFilterKeys(filter);
	modOnFrame(frame);
}
