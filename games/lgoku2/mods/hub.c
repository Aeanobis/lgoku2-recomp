// "Extras": one more page in the game's own Start menu, between Items and
// Options, reached with L and R like the others. Everything the mods add is on
// it, so nothing needs a keyboard key:
//
//   Equip, Shop, Techniques   (shop.c)
//   Stat points               (progression.c)
//   Instant Transmission      (transmission.c)
//   Super Saiyan 3            (looks.c; only while Goku is a Super Saiyan)
//   Enemy toughness           (combat.c)
//   Untouchable               (blocking.c)
//
// (Character looks are on the Switch Characters screen: see looks.c.)
//
// How it is fitted in. The game's menu knows nothing about it. This mod
// watches the picture to tell which page the menu is on (the page's name in
// the middle of the bottom bar). On Items, a press of R is kept from the game
// and this page is drawn over the menu instead; R again lets the press
// through, so the game moves on to Options. Coming back the other way works
// the same with L.
//
// The game is NOT paused while this page or a menu opened from it is up: it
// goes on running its own menu underneath (so the music keeps playing) and is
// simply given no buttons.
#include "lg2.h"

#include <string.h>

enum { BTN_A = 1, BTN_B = 2, BTN_START = 8, BTN_UP = 64, BTN_DOWN = 128, BTN_R = 256, BTN_L = 512 };
enum { WORLD_MAP_ZONE = 6, WORLD_MAP_AREA = 99 };

int lg2HubOpen;

// ---- which page the game's menu is on -------------------------------------------------------
enum { NO_MENU = -1, STATUS, ITEMS, OPTIONS, JOURNAL };

// The page name is white lettering in the middle of the bottom bar: a number
// worked out from where its white pixels are tells the four apart
static int menuPage(void) {
	static const uint32_t names[4] = { 0x4F145F2C, 0xF82D6903, 0xE91518DD, 0x8DD5E93C };
	const uint32_t* screen = modScreen();
	uint32_t hash = 0;
	int x, y, i;
	if (modRead32(LG2_RAM_PLAYER_PTR)) {
		return NO_MENU; // the Start menu takes the player away while it is up
	}
	for (y = 132; y < 146; ++y) {
		for (x = 88; x < 152; ++x) {
			if ((screen[y * 240 + x] & 0xFFFFFF) == 0xFFFFFF) {
				hash = hash * 31 + x * 200 + y;
			}
		}
	}
	for (i = 0; i < 4; ++i) {
		if (hash == names[i]) {
			return i;
		}
	}
	return NO_MENU;
}

// ---- buttons -------------------------------------------------------------------------------------
static int page = NO_MENU; // as of the last frame drawn

int lg2StartMenuPage(void) {
	return page; // -1 none, 0 status, 1 items, 2 options, 3 journal
}
static int wantOpen;       // ITEMS or OPTIONS: which side it is being entered from
static uint32_t hidden;    // buttons kept from the game until they are let go
static uint32_t seen;      // buttons as of the last frame
static int closing;        // frames of "B" still to give the game, to shut its menu
static int passing;        // frames of passKeys still to give the game
static uint32_t passKeys;
static void (*afterwards)(void);

static uint32_t filter(uint32_t keys) {
	uint32_t pressed = keys & ~seen;
	seen = keys;
	hidden &= keys;
	if (!lg2HubOpen && !wantOpen) {
		if (page == ITEMS && (pressed & BTN_R)) {
			wantOpen = ITEMS;
			hidden |= BTN_R;
		} else if (page == OPTIONS && (pressed & BTN_L)) {
			wantOpen = OPTIONS;
			hidden |= BTN_L;
		}
	}
	if (closing) {
		--closing;
		return BTN_B;
	}
	if (lg2HubOpen) {
		return 0; // the buttons are for this page, or the menu it opened
	}
	if (passing) { // a press being handed on to the game's menu
		--passing;
		return (keys & ~hidden) | passKeys;
	}
	return keys & ~hidden;
}

void lg2HubCloseThen(void (*then)(void)) {
	modLog("hub: closing the Start menu%s", then ? ", then carrying on with something" : "");
	afterwards = then;
	lg2HubOpen = 0;
	hidden |= seen | BTN_A; // whatever is held was meant for the page, not the game
	closing = 3;
}

// ---- what is on the page -----------------------------------------------------------------------
enum { EQUIP, SHOP, TECHNIQUES, STAT_POINTS, TRAVEL, BOSS, TOUGHNESS, UNTOUCHABLE, ENTRIES };
static int entries[ENTRIES], nEntries, cursor, enteredFrom;

static void listEntries(void) {
	int onMap = modRead8(LG2_RAM_ZONE) == WORLD_MAP_ZONE && modRead8(LG2_RAM_ZONE + 1) == WORLD_MAP_AREA;
	nEntries = 0;
	entries[nEntries++] = EQUIP;
	entries[nEntries++] = SHOP;
	entries[nEntries++] = TECHNIQUES;
	entries[nEntries++] = STAT_POINTS;
	if (!onMap) {
		entries[nEntries++] = TRAVEL;
	}
	if (lg2BossFightsHere()) {
		entries[nEntries++] = BOSS;
	}
	entries[nEntries++] = TOUGHNESS;
	entries[nEntries++] = UNTOUCHABLE;
	if (cursor >= nEntries) {
		cursor = nEntries - 1;
	}
}

static void choose(int entry) {
	switch (entry) {
	case EQUIP: lg2OpenShop(0); break;
	case SHOP: lg2OpenShop(1); break;
	case TECHNIQUES: lg2OpenShop(2); break;
	case STAT_POINTS: lg2OpenStatMenu(); break;
	case TRAVEL: lg2OpenTransmission(); break;
	case BOSS: lg2OpenBossMenu(); break;
	case TOUGHNESS: lg2NextToughness(); break;
	case UNTOUCHABLE: lg2Untouchable = !lg2Untouchable; break;
	}
}

// ---- drawing -------------------------------------------------------------------------------------
// In the Start menu's own colours: its light blue panel and dark lettering
// box, with the page names along the bottom changed to suit
static void label(int x, int y, int width, const char* text, uint32_t color) {
	modRect(x, y - 1, width, 9, 0x8C318C); // the bar's purple, over the game's own word
	modText(x + (width - 4 * (int) strlen(text)) / 2, y + 1, color, "%s", text);
}

static void draw(void) {
	int i;
	modRect(30, 12, 72, 11, 0xDEB56B);   // over the game's page name on the panel's tab
	modRect(9, 25, 222, 102, 0xDEB56B);  // the panel's blue, over the page's contents
	modRect(16, 31, 208, 90, 0x301008);  // the dark box the game puts lists in
	modRect(18, 33, 204, 86, 0x4A1021);
	modTextPlain(1);
	modTextScale(2);
	modText(42, 12, 0xFFFFFF, "EXTRAS");
	modTextScale(1);
	for (i = 0; i < nEntries; ++i) {
		int y = 38 + i * 8 + (i >= 5 ? 2 : 0);
		uint32_t color = i == cursor ? 0x00BDFF : 0xFFFFFF;
		const char* extra = "";
		char text[48];
		switch (entries[i]) {
		case EQUIP: strcpy(text, "EQUIP"); extra = "WHAT IS WORN"; break;
		case SHOP: strcpy(text, "SHOP"); extra = "BUY GEAR AND SUPPLIES"; break;
		case TECHNIQUES: strcpy(text, "TECHNIQUES"); extra = "LEARN AND CHOOSE SPECIALS"; break;
		case STAT_POINTS: strcpy(text, "STAT POINTS"); extra = lg2StatPointsWaiting() ? "POINTS TO SPEND!" : "NONE TO SPEND"; break;
		case TRAVEL: strcpy(text, "INSTANT TRANSMISSION"); break;
		case BOSS: strcpy(text, "BOSS FIGHT"); extra = "FRIEZA, CELL AND THE REST"; break;
		case TOUGHNESS: strcpy(text, "ENEMIES"); extra = lg2ToughnessName(); break;
		default: strcpy(text, "UNTOUCHABLE"); extra = lg2Untouchable ? "ON" : "OFF"; break;
		}
		modText(22, y, color, "%c %s", i == cursor ? '>' : ' ', text);
		modText(120, y, i == cursor ? 0x00BDFF : 0xC0C0C0, "%s", extra);
	}
	modTextPlain(0);
	// the bottom bar: the pages either side, and this one in the middle
	modTextPlain(1);
	label(22, 144, 56, "ITEMS", 0xFFFFFF);
	label(94, 136, 52, "EXTRAS", 0xFFFFFF);
	label(160, 144, 58, "OPTIONS", 0xFFFFFF);
	modTextPlain(0);
}

// ---- every frame -----------------------------------------------------------------------------------
static void frame(void) {
	static uint32_t lastKeys;
	static int subWasOpen;
	uint32_t keys = modKeys(), pressed = keys & ~lastKeys;
	lastKeys = keys;

	if (!lg2HubOpen) {
		if (!modPaused()) {
			page = menuPage();
		}
		if (wantOpen) {
			if (page == wantOpen && !modPaused()) {
				modLog("hub: Extras page open (from %s)", wantOpen == ITEMS ? "Items" : "Options");
				lg2HubOpen = 1;
				enteredFrom = wantOpen;
				cursor = 0;
			}
			wantOpen = 0;
		}
		// Back in the world: do what was asked for. Not at once: the game is
		// still putting the area's graphics back for a moment after its menu
		// shuts, and travelling in the middle of that garbles the new area.
		static int inWorld;
		inWorld = lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR)) && !modPaused() ? inWorld + 1 : 0;
		if (afterwards && !closing && inWorld >= 45) {
			void (*then)(void) = afterwards;
			afterwards = 0;
			modLog("hub: back in the world: carrying on");
			then();
		}
		if (!lg2HubOpen) {
			return;
		}
		pressed = 0;
	}

	int sub = lg2ShopIsOpen() || lg2StatMenuIsOpen() || lg2TransmissionIsOpen() || lg2BossMenuIsOpen();
	if (sub || subWasOpen) {
		subWasOpen = sub; // a menu opened from here has the screen; its closing press is not ours
		if (!sub) {
			listEntries();
			draw();
		}
		return;
	}
	listEntries();
	if (pressed & BTN_UP) {
		cursor = (cursor + nEntries - 1) % nEntries;
	}
	if (pressed & BTN_DOWN) {
		cursor = (cursor + 1) % nEntries;
	}
	if (pressed & (BTN_R | BTN_L)) {
		// On to the next page. The game's menu is still on the page this was
		// entered from, so the press goes through to it only if that moves it
		// the right way.
		int right = (pressed & BTN_R) != 0;
		int pass = right ? enteredFrom == ITEMS : enteredFrom == OPTIONS;
		if (pass) {
			passKeys = right ? BTN_R : BTN_L;
			passing = 3;
		} else {
			hidden |= right ? BTN_R : BTN_L;
		}
		modLog("hub: Extras page left with %s", right ? "R" : "L");
		lg2HubOpen = 0;
		page = NO_MENU;
		return;
	}
	if (pressed & (BTN_B | BTN_START)) { // out of the Start menu altogether
		lg2HubCloseThen(0);
		return;
	}
	if (pressed & BTN_A) {
		choose(entries[cursor]);
		if (!lg2HubOpen) {
			return;
		}
		listEntries();
	}
	draw();
}

MOD_INIT(hub) {
	modFilterKeys(filter);
	modOnFrame(frame);
}
