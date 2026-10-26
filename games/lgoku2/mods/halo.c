// Goku's halo, as in Buu's Fury, where he is dead for most of the game: a
// gold ring floating above his head. There it is a small sprite of its own
// that follows him, and so it is here, drawn over the picture.
//
// It comes on by itself once the story has Goku die (the Cell Games), and
// can be put on or taken off like any look: on the Switch Characters screen,
// Up or Down with Goku highlighted. The choice is remembered.
#include "lg2.h"

enum { BTN_UP = 64, BTN_DOWN = 128 };
enum { OFF, ON, AUTO };

static int want = AUTO;      // the player's choice: OFF, ON, or AUTO (by the story)
static unsigned messageUntil;

// Whether the story has had Goku die. The game keeps its story as a bit per
// flag in the save data (LG2_RAM_SAVE + 0xB4; triggers test them as
// (kind << 16) | flag); flag 191 opens the Cell Games Arena on the world map.
// Goku dies during the Cell Games, and the game drops him from the party when
// he does: so once the arena is open, the first time Goku goes from being in
// the party to not, he has died, and that is remembered.
#define LG2_RAM_SAVE 0x030010AC
#define ARENA_OPEN_FLAG 191
static int died;

static int storyFlag(int n) {
	return (modRead8(LG2_RAM_SAVE + 0xB4 + n / 8) >> (n % 8)) & 1;
}

static void watchGoku(void) {
	static int wasInParty = -1, givenBack;
	int inParty = (lg2Char(LG2_GOKU)->flags & 1) != 0;
	if (!died && wasInParty == 1 && !inParty && storyFlag(ARENA_OPEN_FLAG)) {
		died = 1;
		modSetSetting("goku_died", 1);
		modLog("halo: Goku has left the party with the Cell Games on: he has died");
	}
	// Dead, he stays playable, as in Buu's Fury (where he is dead for most of
	// the game): the game's own removal of him is undone, and his record is
	// whole, so he comes back at his level with his attacks, halo and all
	if (died && !inParty && !givenBack) {
		lg2Char(LG2_GOKU)->flags |= 1;
		inParty = 1;
		givenBack = 1;
		modLog("halo: Goku, dead, is back in the party");
	}
	wasInParty = inParty;
}

static int dead(void) {
	return died;
}

// Once he has died there is no Goku without it: the look cannot be changed
static int on(void) {
	return dead() || want == ON;
}

// A character's figure on the Switch Characters screen: the actor drawn with
// his form's sprite (there is no player object on that screen)
static uint32_t figureOf(int who) {
	uint32_t def = ((const uint32_t*) modRom(LG2_ROM_SPRITE_DEFS))[lg2FormDef(lg2Char(who)->sprite)];
	const uint32_t* ram = modRam(0x02000000);
	uint32_t i;
	for (i = 0x48 / 4; i + 1 < 0x40000 / 4; ++i) {
		if (ram[i] == def && ram[i + 1] >= def && ram[i + 1] < def + LG2_SPRITE_DEF_ANIMATIONS + 32 * 16) {
			uint32_t actor = 0x02000000 + i * 4 - LG2_ACTOR_SPRITE_DEF;
			int x = (int) modRead32(actor + LG2_ACTOR_SCREEN_X), y = (int) modRead32(actor + LG2_ACTOR_SCREEN_Y);
			if (x > -64 && x < 240 && y > -64 && y < 160) {
				return actor;
			}
		}
	}
	return 0;
}

static void blend(int x, int y, uint32_t color, int amount) { // amount out of 4
	if (x < 0 || x >= 240 || y < 0 || y >= 160) {
		return;
	}
	uint32_t* p = &modScreen()[y * 240 + x];
	int r = *p & 0xFF, g = (*p >> 8) & 0xFF, b = (*p >> 16) & 0xFF;
	r += ((int) (color & 0xFF) - r) * amount / 4;
	g += ((int) ((color >> 8) & 0xFF) - g) * amount / 4;
	b += ((int) ((color >> 16) & 0xFF) - b) * amount / 4;
	*p = r | (g << 8) | (b << 16);
}

// The ring: an ellipse 14 wide and 5 high seen from slightly above, gold with
// a brighter top edge and a darker underside, bobbing with the frames
static void drawHalo(uint32_t player) {
	uint32_t frameRecord = modRead32(player + LG2_ACTOR_FRAME_RECORD);
	int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16;
	int cx = (int) modRead32(player + LG2_ACTOR_SCREEN_X) + w / 2;
	int top = (int) modRead32(player + LG2_ACTOR_SCREEN_Y);
	int bob = ((modFrame() / 20) & 3) == 1 || ((modFrame() / 20) & 3) == 2 ? 1 : 0;
	int cy = top - 5 - bob, x, y;
	for (y = -2; y <= 2; ++y) {
		for (x = -7; x <= 7; ++x) {
			// inside the outer ellipse but outside the inner one
			int outer = x * x * 25 + y * y * 49 <= 49 * 25;            // (x/7)^2 + (y/5*2)^2 <= 1
			int inner = x * x * 25 + y * y * 49 * 4 <= 49 * 25 * 4 / 9; // a thinner one inside
			if (outer && !(x * x * 4 + y * y * 16 <= 36 && y > -2)) {
				uint32_t color = y < 0 ? 0x60F0FF : y == 0 ? 0x40D0F0 : 0x2090C0; // (BGR) pale gold, gold, dark gold
				blend(cx + x, cy + y, color, 4);
			}
			(void) inner;
		}
	}
}

// For other mods: the ring over any actor (heaven.c puts it on Goku there)
void lg2DrawHalo(uint32_t actor) {
	drawHalo(actor);
}

static void frame(void) {
	static uint32_t lastKeys;
	uint32_t pressed = modKeys() & ~lastKeys;
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	lastKeys = modKeys();
	if (modPaused()) {
		return;
	}
	if (lg2IsPlayer(player)) {
		watchGoku();
	}
	if (lg2SwitchScreenOpen() && lg2SwitchScreenChar() == LG2_GOKU && (pressed & (BTN_UP | BTN_DOWN)) && !dead()) {
		want = on() ? OFF : ON;
		modSetSetting("goku_halo", want);
		messageUntil = modFrame() + 120;
	}
	if (lg2SwitchScreenOpen() && lg2SwitchScreenChar() == LG2_GOKU && !dead()) {
		modText(72, 150, 0xC0C0C0, "UP / DOWN: CHANGE LOOK");
	}
	if (lg2SwitchScreenOpen() && on()) {
		// his figure on the roster wears it too
		uint32_t figure = figureOf(LG2_GOKU);
		if (figure) {
			drawHalo(figure);
		}
	}
	if (on() && (player >> 24) == 2 && lg2CurrentChar() == LG2_GOKU && !lg2FusionKind && lg2Char(LG2_GOKU)->hp > 0) {
		drawHalo(player);
	}
	if (modFrame() < messageUntil) {
		modTextScale(2);
		modText(8, 30, 0x40FF40, on() ? "GOKU: HALO" : "GOKU: NO HALO");
		modTextScale(1);
	}
}

MOD_INIT(halo) {
	want = modGetSetting("goku_halo", AUTO);
	died = modGetSetting("goku_died", 0);
	modOnFrame(frame);
}
