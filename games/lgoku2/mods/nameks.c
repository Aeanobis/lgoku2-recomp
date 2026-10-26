// The seven missing Namekians, found without the finding (asked for 2026-10-15).
//
// The game keeps its story as a bit per flag in the save data (LG2_RAM_SAVE +
// 0xB4). Talking to a found Namekian sets one flag of the block 288..295 (the
// one in West City Apartments B set 293, seen by snapshotting the save before
// and after); the block is one column of a table of flag numbers in the ROM
// (0x0802DD40: eight rows of four columns, 272+, 280+, 288+, 296+). The whole
// block is set once, so the quest's "return to Capsule Corporation" is all
// that is left to do.
#include "lg2.h"

#define LG2_RAM_SAVE 0x030010AC
#define FIRST_FLAG 288
#define LAST_FLAG 295

static unsigned messageUntil;

static void setFlag(int n) {
	uint8_t* f = modRam(LG2_RAM_SAVE + 0xB4 + n / 8);
	*f |= (uint8_t) (1 << (n % 8));
}

static int flag(int n) {
	return (modRead8(LG2_RAM_SAVE + 0xB4 + n / 8) >> (n % 8)) & 1;
}

static void frame(void) {
	int n, missing = 0;
	if (modGetSetting("nameks_given", 0) || !lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR))) {
		if (modFrame() < messageUntil && !modPaused()) {
			modTextScale(2);
			modText(8, 30, 0x40FF40, "THE SEVEN NAMEKS: FOUND");
			modTextScale(1);
		}
		return;
	}
	for (n = FIRST_FLAG; n <= LAST_FLAG; ++n) {
		if (!flag(n)) {
			setFlag(n);
			++missing;
		}
	}
	modSetSetting("nameks_given", 1);
	messageUntil = modFrame() + 180;
	modLog("nameks: flags %d..%d set (%d were missing): all seven Namekians count as found", FIRST_FLAG, LAST_FLAG, missing);
}

MOD_INIT(nameks) {
	modOnFrame(frame);
}
