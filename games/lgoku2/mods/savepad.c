// The save pads (the Capsule Corp circles): standing on one fills health and
// energy up and keeps them full, whatever is draining them (a Super Saiyan
// form, Super Saiyan 3 above all). The game fills them when you step on a
// pad; this keeps them there, and looks.c pauses the Super Saiyan 3 drain.
#include "lg2.h"

#define LG2_RAM_AREA_RECORD 0x03000F10 // the current area's record
#define SAVE_PAD_CLASS 0x0801ED13      // the first word of a save pad's trigger record
#define MOST_TRIGGERS 24

// The area's trigger list (header +16): pairs {kind, record}; a save pad's
// record is {class, class, x1, y1, x2, y2 (16-bit)}. The list has no usable
// count: it is read while the records are in ROM.
int lg2OnSavePad(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR), area, list;
	int x, y, k;
	if (!lg2IsPlayer(player)) {
		return 0;
	}
	area = modRead32(LG2_RAM_AREA_RECORD);
	if ((area >> 24) != 8) {
		return 0;
	}
	list = modRead32(area + 16);
	if ((list >> 24) != 8) {
		return 0;
	}
	x = (int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8;
	y = (int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8;
	for (k = 0; k < MOST_TRIGGERS; ++k) {
		uint32_t record = modRead32(list + 8 * k + 4);
		if ((record >> 24) != 8) {
			break;
		}
		if (modRead32(record) == SAVE_PAD_CLASS) {
			int x1 = modRead16(record + 8), y1 = modRead16(record + 10), x2 = modRead16(record + 12), y2 = modRead16(record + 14);
			if (x >= x1 && x <= x2 && y >= y1 - 8 && y <= y2 + 8) {
				return 1;
			}
		}
	}
	return 0;
}

static void frame(void) {
	int who = lg2CurrentChar();
	if (modPaused() || who < 0 || who >= LG2_CHARACTERS || !lg2OnSavePad()) {
		return;
	}
	struct LG2Char* c = lg2Char(who);
	if (c->hp > 0) {
		c->hp = c->maxHp;
		c->ep = c->maxEp;
	}
}

MOD_INIT(savepad) {
	modOnFrame(frame);
}
