// Example: watch game functions being called. Useful for working out what a
// function does before changing it. Copy into ../mods/ and run make.
//
// The addresses below are the functions that read the character stat table
// (0x0802A868) and the experience-per-level table (0x0802A6A0).
#include "mod.h"
#include <stdio.h>

static unsigned calls[4];

#define TRACE(N, ADDR) \
	static int trace##N(uint32_t* r) { \
		if (calls[N]++ < 8) { \
			fprintf(stderr, "frame %u: %08X(r0=%X, r1=%X, r2=%X, r3=%X) called from %08X\n", \
			        modFrame(), ADDR, r[0], r[1], r[2], r[3], r[14] & ~1U); \
		} \
		return MOD_CONTINUE; \
	}
TRACE(0, 0x08008A4E)
TRACE(1, 0x08008B3C)
TRACE(2, 0x08008C44)
TRACE(3, 0x08008CFC)

static void frame(void) {
	modText(2, 152, 0x00FFFF, "XP:%u STAT:%u/%u/%u", calls[0], calls[1], calls[2], calls[3]);
}

MOD_INIT(trace) {
	modHook(0x08008A4E, trace0);
	modHook(0x08008B3C, trace1);
	modHook(0x08008C44, trace2);
	modHook(0x08008CFC, trace3);
	modOnFrame(frame);
}
