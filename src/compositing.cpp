// toBLCompOp() (declared in newui/viewstyle.h): platform-neutral, so shapes/overlays can use it everywhere.
#include "newui/viewstyle.h"

namespace newui {

	BLCompOp toBLCompOp(CompositingFlag f)
	{
		BLCompOp result = (BLCompOp)0;

		switch (f) {
		case CompSrcOver: { result = BL_COMP_OP_SRC_OVER; } break;
			case CompSrcopy: { result = BL_COMP_OP_SRC_COPY; } break;
			case CompSrcIn: { result = BL_COMP_OP_SRC_IN; } break;
			case CompSrcOut: { result = BL_COMP_OP_SRC_OUT; } break;
			case CompSrcAtop: { result = BL_COMP_OP_SRC_ATOP; } break;
			case CompDstOver: { result = BL_COMP_OP_DST_OVER; } break;
			case CompDstCopy: { result = BL_COMP_OP_DST_COPY; } break;
			case CompDstIn: { result = BL_COMP_OP_DST_IN; } break;
			case CompDstOut: { result = BL_COMP_OP_DST_OUT; } break;
			case CompDstAtop: { result = BL_COMP_OP_DST_ATOP; } break;
			case CompXOR: { result = BL_COMP_OP_XOR; } break;
			case CompClear: { result = BL_COMP_OP_CLEAR; } break;
			case CompPlus: { result = BL_COMP_OP_PLUS; } break;
			case CompMinus: { result = BL_COMP_OP_MINUS; } break;
			case CompModulate: { result = BL_COMP_OP_MODULATE; } break;
			case CompMultiply: { result = BL_COMP_OP_MULTIPLY; } break;
			case CompScreen: { result = BL_COMP_OP_SCREEN; } break;
			case CompOverlay: { result = BL_COMP_OP_OVERLAY; } break;
			case CompDarken: { result = BL_COMP_OP_DARKEN; } break;
			case CompLighten: { result = BL_COMP_OP_LIGHTEN; } break;
			case CompColorDodge: { result = BL_COMP_OP_COLOR_DODGE; } break;
			case CompColorBurn: { result = BL_COMP_OP_COLOR_BURN; } break;
			case CompLinearBurn: { result = BL_COMP_OP_LINEAR_BURN; } break;
			case CompLinearLight: { result = BL_COMP_OP_LINEAR_LIGHT; } break;
			case CompPinLight: { result = BL_COMP_OP_PIN_LIGHT; } break;
			case CompHardLight: { result = BL_COMP_OP_HARD_LIGHT; } break;
			case CompSoftLight: { result = BL_COMP_OP_SOFT_LIGHT; } break;
			case CompDifference: { result = BL_COMP_OP_DIFFERENCE; } break;
			case CompExclusion: { result = BL_COMP_OP_EXCLUSION; } break;
		}

		return result;
	}

}
