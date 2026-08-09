
#pragma once
#include <cstdint>


namespace newui {


	/**
	Keyboard masks
	*/
	enum KeyboardMasks {
		kmUndefined = 0,
		kmAlt = 1,
		kmShift = 2,
		kmCtrl = 4
	};


	enum KeyboardEvent {
		keUndefined = 0,
		keKeyDown = 1,
		keKeyUp = 2,
		keKeyPress = 3
	};
	

	/**
	This enum is a mapping of virtual keys on a keyboard
	to a standard value.
	*/
	enum VirtualKeyCode {
		vkUndefined = 0,
		vkF1 = 200,
		vkF2,
		vkF3,
		vkF4,
		vkF5,
		vkF6,
		vkF7,
		vkF8,
		vkF9,
		vkF10,
		vkF11,
		vkF12,
		vkUpArrow,
		vkDownArrow,
		vkLeftArrow,
		vkRightArrow,
		vkPgUp,
		vkPgDown,
		vkHome,
		vkEnd,
		vkInsert,
		vkDelete,
		vkBackSpace,
		vkNumber0,
		vkNumber1,
		vkNumber2,
		vkNumber3,
		vkNumber4,
		vkNumber5,
		vkNumber6,
		vkNumber7,
		vkNumber8,
		vkNumber9,
		vkLetterA,
		vkLetterB,
		vkLetterC,
		vkLetterD,
		vkLetterE,
		vkLetterF,
		vkLetterG,
		vkLetterH,
		vkLetterI,
		vkLetterJ,
		vkLetterK,
		vkLetterL,
		vkLetterM,
		vkLetterN,
		vkLetterO,
		vkLetterP,
		vkLetterQ,
		vkLetterR,
		vkLetterS,
		vkLetterT,
		vkLetterU,
		vkLetterV,
		vkLetterW,
		vkLetterX,
		vkLetterY,
		vkLetterZ,
		vkSpaceBar,
		vkReturn,
		vkAlt,
		vkShift,
		vkCtrl,
		vkTab,
		vkEscape,
		vkLeftApostrophe,			//`
		vkTilde,					//~
		vkExclamation,				//!
		vkCommercialAt,				//@
		vkNumberSign,				//#
		vkDollarSign,				//$
		vkPercent,					//%
		vkCircumflex,				//^
		vkAmpersand,				//&
		vkAsterix,					//*
		vkOpenParen,				//(
		vkCloseParen,				//)
		vkHyphen,					//-
		vkUnderbar,					//_
		vkEqualsSign,				//=
		vkPlusSign,					//+
		vkUprightBar,				//|
		vkBackSlash,				/* \   */
		vkOpenBracket,				//[
		vkOpenBrace,				//{
		vkCloseBracket,				//]
		vkCloseBrace,				//}
		vkSemiColon,
		vkColon,
		vkSingleQuote,
		vkDoubleQuote,
		vkComma,
		vkLessThan,
		vkPeriod,
		vkGreaterThan,
		vkForwardSlash,
		vkQuestionMark,
		//miscellaneous
		vkPrintScreen,
		vkScrollLock,
		vkPause,
		vkCapsLock,
		//substitutions
		vkMinusSign = vkHyphen,
		vkDivideSign = vkForwardSlash,
		vkMultiplySign = vkAsterix,
		vkEnter = vkReturn
	};

}


#define KB_CONTEXT_CODE			29
#define KB_PREVIOUS_STATE		30
#define KB_IS_EXTENDED_KEY		24

#define SHIFT_KEY_DOWN \
	((GetKeyState( VK_SHIFT) & 15 ) == 1)


