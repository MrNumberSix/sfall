/*
 *    sfall
 *    Copyright (C) 2009, 2010  Mash (Matt Wells, mashw at bigpond dot net dot au)
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

#include "..\main.h"
#include "..\FalloutEngine\Fallout2.h"
#include "Inventory.h"
#include "LoadGameHook.h"
#include "Message.h"
#include "PartyControl.h"
#include "ScriptExtender.h"

#include "HeroAppearance.h"

namespace sfall
{

using namespace fo;

bool HeroAppearance::appModEnabled = false; // check if Appearance mod enabled for script functions

const char* appearancePathFmt = "Appearance\\h%cR%02dS%02d%s";

// char scrn surfaces
BYTE *newButtonSurface = nullptr;
BYTE *charScrnBackSurface = nullptr;

// char scrn critter rotation vars
DWORD charRotTick = 0;
DWORD charRotOri = 0;

int currentRaceVal = 0, currentStyleVal = 0;     // holds Appearance values to restore after global reset in NewGame2 function in LoadGameHooks.cpp
DWORD critterListSize = 0, critterArraySize = 0; // Critter art list size

fo::PathNode **tempPathPtr = &fo::var::paths;
fo::PathNode *heroPathPtr = nullptr;
fo::PathNode *racePathPtr = nullptr;

// for word wrapping
typedef struct LineNode {
	DWORD offset;
	LineNode *next;

	LineNode() {
		next = nullptr;
		offset = 0;
	}
} LineNode;

/////////////////////////////////////////////////////////////////TEXT FUNCTIONS//////////////////////////////////////////////////////////////////////

static void SetFont(long ref) {
	fo::func::text_font(ref);
}

static long GetFont() {
	return fo::var::curr_font_num;
}

/*
int WordWrap(char *TextMsg, DWORD lineLength, WORD *lineNum, WORD *lineOffsets) {
	int retVal;
	__asm {
		mov ebx, lineOffsets
		mov ecx, lineNum
		mov edx, lineLength
		mov eax, TextMsg
		call fo::funcoffs::_word_wrap_
		mov retVal, eax
	}
	return retVal;
}
*/

static bool CreateWordWrapList(char *TextMsg, DWORD WrapWidth, DWORD *lineNum, LineNode *StartLine) {
	*lineNum = 1;

	if (fo::GetMaxCharWidth() >= WrapWidth) return false;
	if (fo::GetTextWidth(TextMsg) < WrapWidth) return true;

	DWORD GapWidth = fo::GetCharGapWidth();

	StartLine->next = new LineNode;
	LineNode *NextLine = StartLine->next;

	DWORD lineWidth = 0, wordWidth = 0, i = 0;
	char CurrentChar;

	while (TextMsg[i] != '\0') {
		CurrentChar = TextMsg[i++];

		int cWidth = fo::GetCharWidth(CurrentChar) + GapWidth;
		lineWidth += cWidth;
		wordWidth += cWidth;

		if (lineWidth <= WrapWidth) {
			if (isspace(CurrentChar) || CurrentChar == '-') {
				NextLine->offset = i;
				wordWidth = 0;
			}
		} else {
			if (isspace(CurrentChar)) {
				NextLine->offset = i;
				wordWidth = 0;
			}
			lineWidth = wordWidth;
			wordWidth = 0;
			CurrentChar = '\0';
			*lineNum += 1;
			NextLine->next = new LineNode;
			NextLine = NextLine->next;
		}
		if (TextMsg[i] == '\0') NextLine->offset = 0;
	}
	return true;
}

static void DeleteWordWrapList(LineNode *CurrentLine) {
	LineNode *NextLine = nullptr;

	while (CurrentLine != nullptr) {
		NextLine = CurrentLine->next;
		delete CurrentLine;
		CurrentLine = NextLine;
	}
}

/////////////////////////////////////////////////////////////////DAT FUNCTIONS///////////////////////////////////////////////////////////////////////

static void* LoadDat(char*fileName) {
	return fo::func::dbase_open(fileName);
}

static void UnloadDat(void *dat) {
	fo::func::dbase_close(dat);
}

/////////////////////////////////////////////////////////////////OTHER FUNCTIONS/////////////////////////////////////////////////////////////////////

static DWORD BuildFrmId(DWORD lstRef, DWORD lstNum) {
	return fo::func::art_id(lstRef, lstNum, 0, 0, 0);
}

static void PlayAcm(char *acmName) {
	__asm {
		mov  eax, acmName;
		call fo::funcoffs::gsound_play_sfx_file_;
	}
}

/////////////////////////////////////////////////////////////////APP MOD FUNCTIONS///////////////////////////////////////////////////////////////////

static char GetSex() {
	return (fo::HeroIsFemale()) ? 'F' : 'M';
}

// functions to load and save appearance globals
static void SetAppearanceGlobals(int race, int style) {
	SetGlobalVar("HAp_Race", race);
	SetGlobalVar("HApStyle", style);
}

static void GetAppearanceGlobals(int *race, int *style) {
	*race = GetGlobalVar("HAp_Race");
	*style = GetGlobalVar("HApStyle");
}

static __declspec(noinline) int _stdcall LoadHeroDat(unsigned int race, unsigned int style, bool flush = false) {
	if (flush) fo::func::art_flush();

	if (heroPathPtr->pDat) { // unload previous Dats
		UnloadDat(heroPathPtr->pDat);
		heroPathPtr->pDat = nullptr;
		heroPathPtr->isDat = 0;
	}
	if (racePathPtr->pDat) {
		UnloadDat(racePathPtr->pDat);
		racePathPtr->pDat = nullptr;
		racePathPtr->isDat = 0;
	}

	const char sex = GetSex();

	sprintf_s(heroPathPtr->path, 64, appearancePathFmt, sex, race, style, ".dat");
	int result = GetFileAttributes(heroPathPtr->path);
	if (result != INVALID_FILE_ATTRIBUTES && !(result & FILE_ATTRIBUTE_DIRECTORY)) { // check if Dat exists for selected appearance
		heroPathPtr->pDat = LoadDat(heroPathPtr->path);
		heroPathPtr->isDat = 1;
	} else {
		sprintf_s(heroPathPtr->path, 64, appearancePathFmt, sex, race, style, "");
		if (GetFileAttributes(heroPathPtr->path) == INVALID_FILE_ATTRIBUTES) // check if folder exists for selected appearance
			return -1;
	}

	tempPathPtr = &heroPathPtr; // set path for selected appearance
	heroPathPtr->next = &fo::var::paths[0];

	if (style != 0) {
		sprintf_s(racePathPtr->path, 64, appearancePathFmt, sex, race, 0, ".dat");
		int result = GetFileAttributes(racePathPtr->path);
		if (result != INVALID_FILE_ATTRIBUTES && !(result & FILE_ATTRIBUTE_DIRECTORY)) { // check if Dat exists for selected race base appearance
			racePathPtr->pDat = LoadDat(racePathPtr->path);
			racePathPtr->isDat = 1;
		} else {
			sprintf_s(racePathPtr->path, 64, appearancePathFmt, sex, race, 0, "");
		}

		if (GetFileAttributes(racePathPtr->path) != INVALID_FILE_ATTRIBUTES) { // check if folder/Dat exists for selected race base appearance
			heroPathPtr->next = racePathPtr; // set path for selected race base appearance
			racePathPtr->next = &fo::var::paths[0];
		}
	}
	return 0;
}

// insert hero art path in front of main path structure when loading art
static void __declspec(naked) LoadNewHeroArt() {
	__asm {
		cmp byte ptr ds:[esi], 'r';
		je  isReading;
		mov ecx, FO_VAR_paths;
		jmp setPath;
isReading:
		mov ecx, tempPathPtr;
setPath:
		mov ecx, dword ptr ds:[ecx];
		retn;
	}
}

static void __declspec(naked) CheckHeroExist() {
	__asm {
		cmp  esi, critterArraySize;       // check if loading hero art
		jle  endFunc;
		mov  eax, FO_VAR_art_name;        // critter art file name address (file name)
		call fo::funcoffs::db_access_;    // check art file exists
		test eax, eax;
		jnz  endFunc;

		// if file not found load regular critter art instead
		sub  esi, critterArraySize;
		add  esp, 4;                      // drop func ret address
		mov  eax, 0x4194E2;
		jmp  eax;
endFunc:
		mov  eax, FO_VAR_art_name;
		retn;
	}
}

// adjust base hero art if num below hero art range
static void __declspec(naked) AdjustHeroBaseArt() {
	__asm {
		add eax, critterListSize;
		mov dword ptr ds:[FO_VAR_art_vault_guy_num], eax;
		retn;
	}
}

// adjust armor art if num below hero art range
static void AdjustHeroArmorArt(DWORD fid) {
	if ((fid & 0xF000000) == (fo::OBJ_TYPE_CRITTER << 24) && !PartyControl::IsNpcControlled()) {
		DWORD fidBase = fid & 0xFFF;
		if (fidBase <= critterListSize) {
			fo::var::i_fid += critterListSize;
		}
	}
}

static void _stdcall SetHeroArt(bool newArtFlag) {
	fo::GameObject* hero = fo::var::obj_dude; // hero state struct
	long heroFID = hero->artFid;              // get hero FrmID
	DWORD fidBase = heroFID & 0xFFF;          // mask out current weapon flag

	if (fidBase > critterListSize) {          // check if critter LST index is in Hero range
		if (!newArtFlag) {
			heroFID -= critterListSize;       // shift index down into normal critter range
			hero->artFid = heroFID;
		}
	} else if (newArtFlag) {
		heroFID += critterListSize;           // shift index up into hero range
		hero->artFid = heroFID;               // set new FrmID to hero state struct
	}
}

// return hero art val to normal before saving
static void __declspec(naked) SavCritNumFix() {
	__asm {
		push ecx;
		push edx;
		push eax;
		push 0;                            // set hero FrmID LST index to normal range before saving
		call SetHeroArt;
		pop  eax;
		call fo::funcoffs::obj_save_dude_; // save current hero state structure fuction
		push eax;
		push 1;                            // return hero FrmID LST index back to hero art range after saving hero state structure
		call SetHeroArt;
		pop  eax;
		pop  edx;
		pop  ecx;
		retn;
	}
}

static void __declspec(naked) DoubleArt() {
	__asm {
		cmp dword ptr ss:[esp + 0xCC], 0x510774; // check if loading critter lst. 0x510774 = addr of critter list size val
		jne endFunc;
		shl edi, 1;                              // double critter list size to add room for hero art
endFunc:
		jmp fo::funcoffs::db_fseek_;
	}
}

// create a duplicate list of critter names at the end with an additional '_' character at the beginning of its name
static long _stdcall AddHeroCritNames() { // art_init_
	auto &critterArt = fo::var::art[fo::OBJ_TYPE_CRITTER];
	critterListSize = critterArt.total / 2;
	critterArraySize = critterListSize * 13;

	char *CritList = critterArt.names;              // critter list offset
	char *HeroList = CritList + critterArraySize;   // set start of hero critter list after regular critter list

	memset(HeroList, 0, critterArraySize);

	for (DWORD i = 0; i < critterListSize; i++) {   // copy critter name list to hero name list
		*HeroList = '_';                            // insert a '_' char at the front of new hero critt names. fallout wont load the same name twice
		memcpy(HeroList + 1, CritList, 11);
		HeroList += 13;
		CritList += 13;
	}
	return critterArt.total;
}

///////////////////////////////////////////////////////////////GRAPHICS HERO FUNCTIONS///////////////////////////////////////////////////////////////

static void DrawPC() {
	fo::RedrawObject(fo::var::obj_dude);
}

// scan inventory items for armor and weapons currently being worn or wielded and setup matching FrmID for PC
void UpdateHeroArt() {
	auto iD = fo::var::inven_dude;
	auto iR = fo::var::i_rhand;
	auto iL = fo::var::i_lhand;
	auto iW = fo::var::i_worn;

	fo::var::i_rhand = 0;
	fo::var::i_lhand = 0;
	fo::var::i_worn = 0;

	fo::var::inven_dude = fo::var::obj_dude;
	int invenSize = fo::var::obj_dude->invenSize;
	//fo::var::pud = invenSize;

	for (int itemNum = 0; itemNum < invenSize; itemNum++) {
		fo::GameObject* item = fo::var::obj_dude->invenTable[itemNum].object; // PC inventory item list + itemListOffset

		if (item->flags & fo::ObjectFlag::Right_Hand) {
			fo::var::i_rhand = item;
		}
		else if (item->flags & fo::ObjectFlag::Left_Hand) {
			fo::var::i_lhand = item;
		}
		else if (item->flags & fo::ObjectFlag::Worn) {
			fo::var::i_worn = item;
		}
	}

	// inventory function - setup pc FrmID and store at address _i_fid
	fo::var::obj_dude->artFid = Inventory::adjust_fid_replacement(); // adjust_fid_

	fo::var::inven_dude = iD;
	fo::var::i_rhand = iR;
	fo::var::i_lhand = iL;
	fo::var::i_worn = iW;
}

void _stdcall RefreshPCArt() {
	fo::func::proto_dude_update_gender(); // refresh PC base model art

	UpdateHeroArt();
	DrawPC();
}

void _stdcall LoadHeroAppearance() {
	if (!HeroAppearance::appModEnabled) return;

	GetAppearanceGlobals(&currentRaceVal, &currentStyleVal);
	LoadHeroDat(currentRaceVal, currentStyleVal, true);
	SetHeroArt(true);
	DrawPC();
}

void _stdcall SetNewCharAppearanceGlobals() {
	if (!HeroAppearance::appModEnabled) return;

	if (currentRaceVal > 0 || currentStyleVal > 0) {
		SetAppearanceGlobals(currentRaceVal, currentStyleVal);
	}
}

// op_set_hero_style
void _stdcall SetHeroStyle(int newStyleVal) {
	if (!HeroAppearance::appModEnabled || newStyleVal == currentStyleVal) return;

	if (LoadHeroDat(currentRaceVal, newStyleVal, true) != 0) {  // if new style cannot be set
		if (currentRaceVal == 0 && newStyleVal == 0) {
			currentStyleVal = 0;                                // ignore error if appearance = default
		} else {
			LoadHeroDat(currentRaceVal, currentStyleVal);       // reload original style
		}
	} else {
		currentStyleVal = newStyleVal;
	}
	SetAppearanceGlobals(currentRaceVal, currentStyleVal);
	DrawPC();
}

// op_set_hero_race
void _stdcall SetHeroRace(int newRaceVal) {
	if (!HeroAppearance::appModEnabled || newRaceVal == currentRaceVal) return;

	if (LoadHeroDat(newRaceVal, 0, true) != 0) {          // if new race fails with style at 0
		if (newRaceVal == 0) {
			currentRaceVal = 0;
			currentStyleVal = 0;                          // ignore if appearance = default
		} else {
			LoadHeroDat(currentRaceVal, currentStyleVal); // reload original race & style
		}
	} else {
		currentRaceVal = newRaceVal;
		currentStyleVal = 0;
	}
	SetAppearanceGlobals(currentRaceVal, currentStyleVal); // store new globals
	DrawPC();
}

// Reset Appearance when selecting "Create Character" from the New Char screen
static void __declspec(naked) CreateCharReset() {
	__asm {
		cmp  currentStyleVal, 0;
		jnz  reset;
		cmp  currentRaceVal, 0;
		jz   endFunc;
reset:  // set race and style to defaults
		push edx;
		push ecx;
		xor  eax, eax;
		mov  currentRaceVal, eax;
		mov  currentStyleVal, eax;
		push eax; // flush
		push eax;
		push eax;
		call LoadHeroDat;
		pop  ecx;
		pop  edx;
		call fo::funcoffs::proto_dude_update_gender_;
endFunc:
		mov  eax, 1;
		retn;
	}
}

/////////////////////////////////////////////////////////////////INTERFACE FUNCTIONS/////////////////////////////////////////////////////////////////

static void sub_draw(long subWidth, long subHeight, long fromWidth, long fromHeight, long fromX, long fromY, BYTE *fromBuff,
					 long toWidth, long toHeight, long toX, long toY, BYTE *toBuff, int maskRef) {

	fromBuff += fromY * fromWidth + fromX;
	toBuff += toY * toWidth + toX;

	for (long h = 0; h < subHeight; h++) {
		for (long w = 0; w < subWidth; w++) {
			if (fromBuff[w] != maskRef)
				toBuff[w] = fromBuff[w];
		}
		fromBuff += fromWidth;
		toBuff += toWidth;
	}
}

static void DrawBody(DWORD critNum, BYTE* surface) {
	DWORD critFrmLock;

	fo::FrmHeaderData *critFrm = fo::func::art_ptr_lock(BuildFrmId(1, critNum), &critFrmLock);
	DWORD critWidth = fo::func::art_frame_width(critFrm, 0, charRotOri);
	DWORD critHeight = fo::func::art_frame_length(critFrm, 0, charRotOri);
	BYTE *critSurface = fo::func::art_frame_data(critFrm, 0, charRotOri);
	sub_draw(critWidth, critHeight, critWidth, critHeight, 0, 0, critSurface, 70, 102, 35 - critWidth / 2, 51 - critHeight / 2, surface, 0);

	fo::func::art_ptr_unlock(critFrmLock);
	critSurface = nullptr;
}

static void DrawPCConsole() {
	DWORD NewTick = *(DWORD*)0x5709C4;  // char scrn gettickcount ret
	DWORD RotSpeed = *(DWORD*)0x47066B; // get rotation speed - inventory rotation speed

	if (charRotTick > NewTick) charRotTick = NewTick;

	if (NewTick - charRotTick > RotSpeed) {
		charRotTick = NewTick;
		if (charRotOri < 5) {
			charRotOri++;
		} else {
			charRotOri = 0;
		}

		int WinRef = fo::var::edit_win; // char screen window ref
		//BYTE *WinSurface = GetWinSurface(WinRef);

		fo::Window *WinInfo = fo::func::GNW_find(WinRef);

		BYTE *ConSurface = new BYTE [70 * 102];
		sub_draw(70, 102, 640, 480, 338, 78, charScrnBackSurface, 70, 102, 0, 0, ConSurface, 0);

		//DWORD critNum = fo::var::art_vault_guy_num; // pointer to current base hero critter FrmId
		DWORD critNum = fo::var::obj_dude->artFid; // pointer to current armored hero critter FrmId
		DrawBody(critNum, ConSurface);

		sub_draw(70, 102, 70, 102, 0, 0, ConSurface, WinInfo->width, WinInfo->height, 338, 78, WinInfo->surface, 0);

		delete[] ConSurface;
		WinInfo = nullptr;

		fo::func::win_draw(WinRef);
	}
}

/*
void DrawCharNote(DWORD LstNum, char *TitleTxt, char *AltTitleTxt, char *Message) {
	__asm {
		mov  ecx, message      //dword ptr ds:[FO_VAR_folder_card_desc]
		mov  ebx, alttitletxt  //dword ptr ds:[FO_VAR_folder_card_title2]
		mov  edx, titletxt     //dword ptr ds:[FO_VAR_folder_card_title]
		mov  eax, lstnum       //dword ptr ds:[FO_VAR_folder_card_fid]
		call fo::funcoffs::drawcard_
	}
}
*/

static void DrawCharNote(bool style, int winRef, DWORD xPosWin, DWORD yPosWin, BYTE *BGSurface, DWORD xPosBG, DWORD yPosBG, DWORD widthBG, DWORD heightBG) {
	fo::MessageList MsgList;
	char *TitleMsg = nullptr;
	char *InfoMsg = nullptr;

	char *MsgFileName = (style) ? "game\\AppStyle.msg" : "game\\AppRace.msg";

	if (fo::func::message_load(&MsgList, MsgFileName) == 1) {
		TitleMsg = GetMsg(&MsgList, 100, 2);
		InfoMsg = GetMsg(&MsgList, 101, 2);
	}

	fo::Window *winInfo = fo::func::GNW_find(winRef);

	BYTE *PadSurface = new BYTE [280 * 168];
	sub_draw(280, 168, widthBG, heightBG, xPosBG, yPosBG, BGSurface, 280, 168, 0, 0, PadSurface, 0);

	UnlistedFrm *frm = LoadUnlistedFrm((style) ? "AppStyle.frm" : "AppRace.frm", fo::OBJ_TYPE_SKILLDEX);
	if (frm) {
		sub_draw(frm->frames[0].width, frm->frames[0].height, frm->frames[0].width, frm->frames[0].height, 0, 0, frm->frames[0].indexBuff, 280, 168, 136, 37, PadSurface, 0); // cover buttons pics bottom
		delete frm;
	}

	int oldFont = GetFont(); // store current font
	SetFont(0x66);           // set font for title

	DWORD textHeight;
	BYTE colour = *(BYTE*)FO_VAR_colorTable; // black color

	if (TitleMsg != nullptr) {
		textHeight = fo::GetTextHeight();
		fo::PrintText(TitleMsg, colour, 0, 0, 265, 280, PadSurface);
		// draw line
		memset(PadSurface + 280 * textHeight, colour, 265);
		memset(PadSurface + 280 * (textHeight + 1), colour, 265);
	}

	DWORD lineNum = 0;
	LineNode *StartLine = new LineNode;
	LineNode *CurrentLine, *NextLine;

	if (InfoMsg != nullptr) {
		SetFont(0x65); // set font for info
		textHeight = fo::GetTextHeight();

		if (CreateWordWrapList(InfoMsg, 160, &lineNum, StartLine)) {
			int lineHeight = 43;

			if (lineNum == 1) {
				fo::PrintText(InfoMsg, colour, 0, lineHeight, 280, 280, PadSurface);
			} else {
				if (lineNum > 11) lineNum = 11;
				CurrentLine = StartLine;

				for (DWORD line = 0; line < lineNum; line++) {
					NextLine = CurrentLine->next;
					char TempChar = InfoMsg[NextLine->offset]; //[line+1]];
					InfoMsg[NextLine->offset] = '\0';
					fo::PrintText(InfoMsg + CurrentLine->offset, colour, 0, lineHeight, 280, 280, PadSurface);
					InfoMsg[NextLine->offset] = TempChar;
					lineHeight += textHeight + 1;
					CurrentLine = NextLine;
				}
			}
		}
	}
	sub_draw(280, 168, 280, 168, 0, 0, PadSurface, winInfo->width, winInfo->height, xPosWin, yPosWin, winInfo->surface, 0);

	SetFont(oldFont); // restore previous font
	fo::func::message_exit(&MsgList);

	*(long*)FO_VAR_card_old_fid1 = -1; // reset fid

	DeleteWordWrapList(StartLine);
	delete[]PadSurface;
	CurrentLine = nullptr;
	NextLine = nullptr;
	winInfo = nullptr;
}

static void _stdcall DrawCharNoteNewChar(bool type) {
	DrawCharNote(type, fo::var::edit_win, 348, 272, charScrnBackSurface, 348, 272, 640, 480);
}

// op_hero_select_win
void _stdcall HeroSelectWindow(int raceStyleFlag) {
	if (!HeroAppearance::appModEnabled) return;

	UnlistedFrm *frm = LoadUnlistedFrm("AppHeroWin.frm", fo::OBJ_TYPE_INTRFACE);
	if (frm == nullptr) {
		fo::func::debug_printf("\nApperanceMod: art\\intrface\\AppHeroWin.frm file not found.");
		return;
	}

	bool isStyle = (raceStyleFlag != 0);
	DWORD resWidth = *(DWORD*)0x4CAD6B;
	DWORD resHeight = *(DWORD*)0x4CAD66;

	int winRef = fo::func::win_add(resWidth / 2 - 242, (resHeight - 100) / 2 - 65, 484, 230, 100, 0x4);
	if (winRef == -1) {
		delete frm;
		return;
	}

	int mouseWasHidden = fo::var::mouse_is_hidden;
	if (mouseWasHidden) fo::func::mouse_show();
	int oldMouse = fo::var::gmouse_current_cursor;
	fo::func::gmouse_set_cursor(1);

	BYTE *winSurface = fo::func::win_get_buf(winRef);
	BYTE *mainSurface = new BYTE [484 * 230];

	sub_draw(484, 230, 484, 230, 0, 0, frm->frames[0].indexBuff, 484, 230, 0, 0, mainSurface, 0);
	delete frm;

	DWORD MenuUObj, MenuDObj;
	BYTE *MenuUSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 299), 0, 0, &MenuUObj); // MENUUP Frm
	BYTE *MenuDSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 300), 0, 0, &MenuDObj); // MENUDOWN Frm
	fo::func::win_register_button(winRef, 115, 181, 26, 26, -1, -1, -1, 0x0D, MenuUSurface, MenuDSurface, 0, 0x20);

	DWORD DidownUObj, DidownDObj;
	BYTE *DidownUSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 93), 0, 0, &DidownUObj); // MENUUP Frm
	BYTE *DidownDSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 94), 0, 0, &DidownDObj); // MENUDOWN Frm
	fo::func::win_register_button(winRef, 25, 84, 24, 25, -1, -1, -1, 0x150, DidownUSurface, DidownDSurface, 0, 0x20);

	DWORD DiupUObj, DiupDObj;
	BYTE *DiupUSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 100), 0, 0, &DiupUObj); // MENUUP Frm
	BYTE *DiupDSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 101), 0, 0, &DiupDObj); // MENUDOWN Frm
	fo::func::win_register_button(winRef, 25, 59, 23, 24, -1, -1, -1, 0x148, DiupUSurface, DiupDSurface, 0, 0x20);

	int oldFont = GetFont();
	SetFont(0x67);

	char titleText[16];
	// Get alternate text from ini if available
	if (isStyle) {
		Translate("AppearanceMod", "StyleText", "Style", titleText, 16);
	} else {
		Translate("AppearanceMod", "RaceText", "Race", titleText, 16);
	}

	BYTE textColour = fo::var::PeanutButter; // PeanutButter colour - palette offset stored in mem
	DWORD titleTextWidth = fo::GetTextWidth(titleText);
	fo::PrintText(titleText, textColour, 92 - titleTextWidth / 2, 10, titleTextWidth, 484, mainSurface);

	Translate("AppearanceMod", "DoneBtn", "Done", titleText, 16);
	titleTextWidth = fo::GetTextWidth(titleText);
	fo::PrintText(titleText, textColour, 80 - titleTextWidth / 2, 185, titleTextWidth, 484, mainSurface);

	sub_draw(484, 230, 484, 230, 0, 0, mainSurface, 484, 230, 0, 0, winSurface, 0);
	fo::func::win_show(winRef);

	SetFont(0x65);

	BYTE *ConDraw = new BYTE [70 * 102];

	int button = 0;
	bool drawFlag = true; // redraw flag for char note pad

	DWORD RotSpeed = *(DWORD*)0x47066B; // get rotation speed - inventory rotation speed
	DWORD RedrawTick = 0, NewTick = 0, OldTick = 0;

	DWORD critNum = fo::var::art_vault_guy_num; // pointer to current base hero critter FrmID
	//DWORD critNum = fo::var::obj_dude->artFID;  // pointer to current armored hero critter FrmID

	int raceVal = currentRaceVal, styleVal = currentStyleVal; // show default style when setting race
	if (!isStyle) styleVal = 0;
	LoadHeroDat(raceVal, styleVal, true);

	SetLoopFlag(LoopFlag::HEROWIN);

	while (true) {                // main loop
		NewTick = GetTickCount(); // timer for redraw
		if (OldTick > NewTick) OldTick = NewTick;

		if (NewTick - OldTick > RotSpeed) { // time to rotate critter
			OldTick = NewTick;
			if (charRotOri < 5)
				charRotOri++;
			else
				charRotOri = 0;
		}
		if (RedrawTick > NewTick) RedrawTick = NewTick;

		if (NewTick - RedrawTick > 60) { // time to redraw
			RedrawTick = NewTick;

			sub_draw(70, 102, 484, 230, 66, 53, mainSurface, 70, 102, 0, 0, ConDraw, 0);
			DrawBody(critNum, ConDraw);
			sub_draw(70, 102, 70, 102, 0, 0, ConDraw, 484, 230, 66, 53, winSurface, 0);

			if (drawFlag) {
				DrawCharNote(isStyle, winRef, 190, 29, mainSurface, 190, 29, 484, 230);
				drawFlag = false;
			}
			fo::func::win_draw(winRef);
		}

		button = fo::func::get_input();
		if (button == 0x148) { // previous style/race -up arrow button pushed
			PlayAcm("ib1p1xx1");

			if (isStyle) {
				if (styleVal == 0) continue;
				styleVal--;
				if (LoadHeroDat(raceVal, styleVal, true) != 0) {
					styleVal = 0;
					LoadHeroDat(raceVal, styleVal);
				}
				drawFlag = true;
			} else { // Race
				if (raceVal == 0) continue;
				raceVal--;
				styleVal = 0;
				if (LoadHeroDat(raceVal, styleVal, true) != 0) {
					raceVal = 0;
					LoadHeroDat(raceVal, styleVal);
				}
				drawFlag = true;
			}
		} else if (button == 0x150) { // Next style/race - down arrow button pushed
			PlayAcm("ib1p1xx1");

			if (isStyle) {
				styleVal++;
				if (LoadHeroDat(raceVal, styleVal, true) != 0) {
					styleVal--;
					LoadHeroDat(raceVal, styleVal);
				} else {
					drawFlag = true;
				}
			} else { // Race
				raceVal++;
				if (LoadHeroDat(raceVal, 0, true) != 0) {
					raceVal--;
					LoadHeroDat(raceVal, styleVal);
				} else {
					styleVal = 0;
					drawFlag = true;
				}
			}
		} else if (button == 0x0D) { // save and exit - Enter button pushed
			PlayAcm("ib1p1xx1");
			if (!isStyle && currentRaceVal == raceVal) { // return style to previous value if no race change
				styleVal = currentStyleVal;
			}
			currentRaceVal = raceVal;
			currentStyleVal = styleVal;
			break;
		} else if (button == 0x1B) { // exit - ESC button pushed
			break;
		}
	}

	ClearLoopFlag(LoopFlag::HEROWIN);

	LoadHeroDat(currentRaceVal, currentStyleVal, true);
	SetAppearanceGlobals(currentRaceVal, currentStyleVal);

	fo::func::win_delete(winRef);
	delete[]mainSurface;
	delete[]ConDraw;

	fo::func::art_ptr_unlock(MenuUObj);
	fo::func::art_ptr_unlock(MenuDObj);
	MenuUSurface = nullptr;
	MenuDSurface = nullptr;

	fo::func::art_ptr_unlock(DidownUObj);
	fo::func::art_ptr_unlock(DidownDObj);
	DidownUSurface = nullptr;
	DidownDSurface = nullptr;

	fo::func::art_ptr_unlock(DiupUObj);
	fo::func::art_ptr_unlock(DiupDObj);
	DiupUSurface = nullptr;
	DiupDSurface = nullptr;

	SetFont(oldFont);
	fo::func::gmouse_set_cursor(oldMouse);

	if (mouseWasHidden) fo::func::mouse_hide();
}

static void FixTextHighLight() {
	__asm {
		// redraw special text
		mov  eax, 7;
		xor  ebx, ebx;
		xor  edx, edx;
		call fo::funcoffs::PrintBasicStat_;
		// redraw trait options text
		call fo::funcoffs::ListTraits_;
		// redraw skills text
		xor  eax, eax;
		call fo::funcoffs::ListSkills_;
		// redraw level text
		call fo::funcoffs::PrintLevelWin_;
		// redraw perks, karma, kill text
		call fo::funcoffs::DrawFolder_;
		// redraw hit points to crit chance text
		call fo::funcoffs::ListDrvdStats_;
		// redraw note pad area text
		//call fo::funcoffs::DrawInfoWin_;
	}
}

static int _stdcall CheckCharButtons() {
	int button = fo::func::get_input();

	int drawFlag = -1;

	int infoLine = fo::var::info_line;
	if (infoLine == 0x503 || infoLine == 0x504) {
		fo::var::info_line -= 2;
		*(DWORD*)FO_VAR_frstc_draw1 = 1;
		DrawCharNoteNewChar(infoLine != 0x503);
	} else if (infoLine == 0x501 || infoLine == 0x502) {
		switch (button) {
		case 0x14B: // button left
		case 0x14D: // button right
			if (fo::var::glblmode == 1) { // if in char creation scrn
				if (infoLine == 0x501) {
					button = button + 0x3C6;
				} else if (infoLine == 0x502) {
					button = button + 0x3C6 + 1;
				}
			}
			break;
		case 0x148: // button up
		case 0x150: // button down
			if (infoLine == 0x501) {
				button = 0x502;
			} else if (infoLine == 0x502) {
				button = 0x501;
			}
			break;
		case 0x0D:  // button return
		case 0x1B:  // button esc
		case 0x1F4: // button done
		case 'd':   // button done
		case 'D':   // button done
		case 0x1F6: // button cancel
		case 'c':   // button cancel
		case 'C':   // button cancel
			fo::var::info_line += 2; // 0x503/0x504 for redrawing note when reentering char screen
			break;
		}
	}

	switch (button) {
	case 0x9: // tab button pushed
		if (infoLine < 0x3D || infoLine >= 0x4F) { // if menu ref in last menu go to race
			break;
		}
		button = 0x501;
	case 0x501: // race title button pushed
		if (infoLine != 0x501) {
			fo::var::info_line = 0x501;
			drawFlag = 3;
		}
		break;
	case 0x502: // style title button pushed
		if (infoLine != 0x502) {
			fo::var::info_line = 0x502;
			drawFlag = 2;
		}
		break;
	case 0x511: // race left button pushed
		if (currentRaceVal == 0) {
			drawFlag = 4;
			break;
		}
		currentStyleVal = 0; // reset style
		currentRaceVal--;
		if (LoadHeroDat(currentRaceVal, currentStyleVal, true) != 0) {
			currentRaceVal = 0;
			LoadHeroDat(currentRaceVal, currentStyleVal);
		}
		drawFlag = 1;
		break;
	case 0x513: // race right button pushed
		currentRaceVal++;

		if (LoadHeroDat(currentRaceVal, 0, true) != 0) {
			currentRaceVal--;
			LoadHeroDat(currentRaceVal, currentStyleVal);
			drawFlag = 4;
		} else {
			currentStyleVal = 0; // reset style
			drawFlag = 1;
		}
		break;
	case 0x512: // style left button pushed
		if (currentStyleVal == 0) {
			drawFlag = 4;
			break;
		}
		currentStyleVal--;
		if (LoadHeroDat(currentRaceVal, currentStyleVal, true)) {
			currentStyleVal = 0;
			LoadHeroDat(currentRaceVal, currentStyleVal, true);
		}
		drawFlag = 0;
		break;
	case 0x514: // style right button pushed
		currentStyleVal++;

		if (LoadHeroDat(currentRaceVal, currentStyleVal, true) != 0) {
			currentStyleVal--;
			LoadHeroDat(currentRaceVal, currentStyleVal);
			drawFlag = 4;
		} else {
			drawFlag = 0;
		}
		break;
	}

	if (drawFlag != -1) {
		bool style = false; // Race;
		switch (drawFlag) {
		case 0:
			fo::var::info_line = 0x502;
			style = true;
			goto play;
		case 1:
			fo::var::info_line = 0x501;
		play:
			PlayAcm("ib3p1xx1");
			break;
		case 2:
			style = true;
		case 3:
			PlayAcm("ISDXXXX1");
			break;
		default:
			PlayAcm("IB3LU1X1");
			return button;
		}
		FixTextHighLight();
		DrawCharNoteNewChar(style);
	}
	DrawPCConsole();

	return button;
}

static void __declspec(naked) CheckCharScrnButtons() {
	__asm {
		call CheckCharButtons;
		cmp  eax, 0x500;
		jl   endFunc;
		cmp  eax, 0x515;
		jg   endFunc;
		add  esp, 4;   // ditch old ret addr
		push 0x431E8A; // recheck buttons if app mod button
endFunc:
		retn;
	}
}

static void __fastcall HeroGenderChange(long gender) {
	// get PC stat current gender
	long newGender = fo::func::stat_level(fo::var::obj_dude, fo::STAT_gender);
	if (newGender == gender) return;      // check if gender has been changed

	long baseModel = (newGender)          // check if male 0
		? *(DWORD*)0x5108AC               // base female model
		: fo::var::art_vault_person_nums; // base male model

	// adjust base hero art
	baseModel += critterListSize;
	fo::var::art_vault_guy_num = baseModel;

	// reset race and style to defaults
	currentRaceVal = 0;
	currentStyleVal = 0;
	LoadHeroDat(0, 0);

	fo::func::proto_dude_update_gender();

	// Check If Race or Style selected to redraw info note
	int infoLine = fo::var::info_line;
	if (infoLine == 0x501 || infoLine == 0x502) {
		DrawCharNoteNewChar(infoLine != 0x501);
	}
}

static void __declspec(naked) SexScrnEnd() {
	using namespace fo;
	__asm {
		push edx;
		mov  edx, STAT_gender;
		mov  eax, dword ptr ds:[FO_VAR_obj_dude];
		call fo::funcoffs::stat_level_; // get PC stat current gender
		mov  ecx, eax;                  // gender
		call fo::funcoffs::SexWindow_;  // call gender selection window
/*
		xor  ebx, ebx;
		cmp byte ptr ds:[FO_VAR_gmovie_played_list + 0x3], 1 // check if wearing vault suit
		jne NoVaultSuit
		mov ebx, 0x8
NoVaultSuit:
		mov  eax, dword ptr ds:[ebx + FO_VAR_art_vault_person_nums]; // base male model
*/
		call HeroGenderChange;
		pop  edx;
		retn;
	}
}

// Create race and style selection buttons when creating a character
static void __declspec(naked) AddCharScrnButtons() {
	__asm {
		pushad; // prolog
		mov  ebp, esp;
		sub  esp, __LOCAL_SIZE;
	}

	int WinRef;
	WinRef = fo::var::edit_win; // char screen window ref

	// race and style title buttons
	fo::func::win_register_button(WinRef, 332, 0, 82, 32, -1, -1, 0x501, -1, 0, 0, 0, 0);
	fo::func::win_register_button(WinRef, 332, 226, 82, 32, -1, -1, 0x502, -1, 0, 0, 0, 0);

	if (fo::var::glblmode == 1) { // equals 1 if new char screen - equals 0 if ingame char screen
		if (newButtonSurface == nullptr) {
			newButtonSurface = new BYTE [20 * 18 * 4];

			DWORD frmLock; // frm objects for char screen Appearance button
			BYTE *frmSurface;

			frmSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 122), 0, 0, &frmLock); // SLUFrm
			sub_draw(20, 18, 20, 18, 0, 0, frmSurface, 20, 18 * 4, 0, 0, newButtonSurface, 0);
			fo::func::art_ptr_unlock(frmLock);

			frmSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 123), 0, 0, &frmLock); // SLDFrm
			sub_draw(20, 18, 20, 18, 0, 0, frmSurface, 20, 18 * 4, 0, 18, newButtonSurface, 0);
			fo::func::art_ptr_unlock(frmLock);

			frmSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 124), 0, 0, &frmLock); // SRUFrm
			sub_draw(20, 18, 20, 18, 0, 0, frmSurface, 20, 18 * 4, 0, 18 * 2, newButtonSurface, 0);
			fo::func::art_ptr_unlock(frmLock);

			frmSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 125), 0, 0, &frmLock); // SRDFrm
			sub_draw(20, 18, 20, 18, 0, 0, frmSurface, 20, 18 * 4, 0, 18 * 3, newButtonSurface, 0);
			fo::func::art_ptr_unlock(frmLock);

			frmSurface = nullptr;
		}

		// check if Data exists for other races male or female, and if so enable race selection buttons
		if (GetFileAttributes("Appearance\\hmR01S00") != INVALID_FILE_ATTRIBUTES || GetFileAttributes("Appearance\\hfR01S00") != INVALID_FILE_ATTRIBUTES ||
			GetFileAttributes("Appearance\\hmR01S00.dat") != INVALID_FILE_ATTRIBUTES || GetFileAttributes("Appearance\\hfR01S00.dat") != INVALID_FILE_ATTRIBUTES) {
			// race selection buttons
			fo::func::win_register_button(WinRef, 348, 37, 20, 18, -1, -1, -1, 0x511, newButtonSurface, newButtonSurface + (20 * 18), 0, 0x20);
			fo::func::win_register_button(WinRef, 373, 37, 20, 18, -1, -1, -1, 0x513, newButtonSurface + (20 * 18 * 2), newButtonSurface + (20 * 18 * 3), 0, 0x20);
		}
		// check if Data exists for other styles male or female, and if so enable style selection buttons
		if (GetFileAttributes("Appearance\\hmR00S01") != INVALID_FILE_ATTRIBUTES || GetFileAttributes("Appearance\\hfR00S01") != INVALID_FILE_ATTRIBUTES ||
			GetFileAttributes("Appearance\\hmR00S01.dat") != INVALID_FILE_ATTRIBUTES || GetFileAttributes("Appearance\\hfR00S01.dat") != INVALID_FILE_ATTRIBUTES) {
			// style selection buttons
			fo::func::win_register_button(WinRef, 348, 199, 20, 18, -1, -1, -1, 0x512, newButtonSurface, newButtonSurface + (20 * 18), 0, 0x20);
			fo::func::win_register_button(WinRef, 373, 199, 20, 18, -1, -1, -1, 0x514, newButtonSurface + (20 * 18 * 2), newButtonSurface + (20 * 18 * 3), 0, 0x20);
		}
	}

	__asm {
		mov  esp, ebp; // epilog
		popad;
		// move tag skills button to fit Appearance interface
		mov  edx, 396 + 30; // tag/skills button xpos offset
		retn;
	}
}

// Loading or creating a background image for the character creation/editing interface
static void __declspec(naked) FixCharScrnBack() {
	__asm {
		mov  dword ptr ds:[FO_VAR_bckgnd], eax; // surface ptr for char scrn back
		test eax, eax;                          // check if frm loaded ok
		je   endFunc;
		// prolog
		pushad;
		mov  ebp, esp;
		sub  esp, __LOCAL_SIZE;
	}

	if (charScrnBackSurface == nullptr) {
		charScrnBackSurface = new BYTE [640 * 480];

		UnlistedFrm *frm = LoadUnlistedFrm((fo::var::glblmode) ? "AppChCrt.frm" : "AppChEdt.frm", fo::OBJ_TYPE_INTRFACE);

		if (frm != nullptr) {
			sub_draw(640, 480, 640, 480, 0, 0, frm->frames[0].indexBuff, 640, 480, 0, 0, charScrnBackSurface, 0);
			delete frm;
		} else {
			BYTE *OldCharScrnBackSurface = fo::var::bckgnd; // char screen background frm surface

			// copy old charscrn surface to new
			sub_draw(640, 480, 640, 480, 0, 0, OldCharScrnBackSurface, 640, 480, 0, 0, charScrnBackSurface, 0);

			// copy Tag Skill Counter background to the right
			sub_draw(38, 26, 640, 480, 519, 228, OldCharScrnBackSurface, 640, 480, 519 + 36, 228, charScrnBackSurface, 0);

			// copy a blank part of the Tag Skill Bar hiding the old counter
			sub_draw(38, 26, 640, 480, 460, 228, OldCharScrnBackSurface, 640, 480, 519, 228, charScrnBackSurface, 0);

			sub_draw(36, 258, 640, 480, 332, 0, OldCharScrnBackSurface, 640, 480, 408, 0, charScrnBackSurface, 0); // shift behind button rail
			sub_draw(6, 32, 640, 480, 331, 233, OldCharScrnBackSurface, 640, 480, 330, 6, charScrnBackSurface, 0); // shadow for style/race button

			DWORD FrmObj, FrmMaskObj; // frm objects for char screen Appearance button
			BYTE *FrmSurface, *FrmMaskSurface;

			FrmSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 113), 0, 0, &FrmObj);
			sub_draw(81, 132, 292, 376, 163, 20, FrmSurface, 640, 480, 331, 63, charScrnBackSurface, 0);  // char view win
			sub_draw(79, 31, 292, 376, 154, 228, FrmSurface, 640, 480, 331, 32, charScrnBackSurface, 0);  // upper  char view win
			sub_draw(79, 30, 292, 376, 158, 236, FrmSurface, 640, 480, 331, 195, charScrnBackSurface, 0); // lower  char view win
			fo::func::art_ptr_unlock(FrmObj);

			// Sexoff Frm
			FrmSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 188), 0, 0, &FrmObj);
			// Sex button mask frm
			FrmMaskSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 187), 0, 0, &FrmMaskObj);

			sub_draw(80, 28, 80, 32, 0, 0, FrmMaskSurface, 80, 32, 0, 0, FrmSurface, 0x39); // mask for style and race buttons
			fo::func::art_ptr_unlock(FrmMaskObj);
			FrmMaskSurface = nullptr;

			FrmSurface[80 * 32 - 1] = 0;
			FrmSurface[80 * 31 - 1] = 0;
			FrmSurface[80 * 30 - 1] = 0;

			FrmSurface[80 * 32 - 2] = 0;
			FrmSurface[80 * 31 - 2] = 0;
			FrmSurface[80 * 30 - 2] = 0;

			FrmSurface[80 * 32 - 3] = 0;
			FrmSurface[80 * 31 - 3] = 0;
			FrmSurface[80 * 30 - 3] = 0;

			FrmSurface[80 * 32 - 4] = 0;
			FrmSurface[80 * 31 - 4] = 0;
			FrmSurface[80 * 30 - 4] = 0;

			sub_draw(80, 32, 80, 32, 0, 0, FrmSurface, 640, 480, 332, 0, charScrnBackSurface, 0);   // style and race buttons
			sub_draw(80, 32, 80, 32, 0, 0, FrmSurface, 640, 480, 332, 225, charScrnBackSurface, 0); // style and race buttons
			fo::func::art_ptr_unlock(FrmObj);

			// frm background for char screen Appearance button
			FrmSurface = fo::func::art_ptr_lock_data(BuildFrmId(6, 174), 0, 0, &FrmObj);                  // Pickchar frm
			sub_draw(69, 20, 640, 480, 282, 320, FrmSurface, 640, 480, 337, 37, charScrnBackSurface, 0);  // button backround top
			sub_draw(69, 20, 640, 480, 282, 320, FrmSurface, 640, 480, 337, 199, charScrnBackSurface, 0); // button backround bottom
			sub_draw(47, 16, 640, 480, 94, 394, FrmSurface, 640, 480, 347, 39, charScrnBackSurface, 0);   // cover buttons pics top
			sub_draw(47, 16, 640, 480, 94, 394, FrmSurface, 640, 480, 347, 201, charScrnBackSurface, 0);  // cover buttons pics bottom
			fo::func::art_ptr_unlock(FrmObj);
			FrmSurface = nullptr;
		}

		int oldFont = GetFont();
		SetFont(0x67);

		char RaceText[8], StyleText[8];
		// Get alternate text from ini if available
		Translate("AppearanceMod", "RaceText", "Race", RaceText, 8);
		Translate("AppearanceMod", "StyleText", "Style", StyleText, 8);

		DWORD raceTextWidth = fo::GetTextWidth(RaceText);
		DWORD styleTextWidth = fo::GetTextWidth(StyleText);

		BYTE PeanutButter = fo::var::PeanutButter; // palette offset stored in mem

		fo::PrintText(RaceText, PeanutButter, 372 - raceTextWidth / 2, 6, raceTextWidth, 640, charScrnBackSurface);
		fo::PrintText(StyleText, PeanutButter, 372 - styleTextWidth / 2, 231, styleTextWidth, 640, charScrnBackSurface);
		SetFont(oldFont);
	}

	fo::var::bckgnd = charScrnBackSurface; // surface ptr for char scrn back

	__asm {
		mov esp, ebp; // epilog
		popad;
endFunc:
		retn;
	}
}

static void DeleteCharSurfaces() {
	delete[] newButtonSurface;
	newButtonSurface = nullptr;

	delete[] charScrnBackSurface;
	charScrnBackSurface = nullptr;
}

static void __declspec(naked) CharScrnEnd() {
	__asm {
		push eax;
		call DeleteCharSurfaces;
		pop  eax;
		mov  ebp, dword ptr ds:[FO_VAR_info_line];
		retn;
	}
}

//////////////////////////////////////////////////////////////////////FIX FUNCTIONS//////////////////////////////////////////////////////////////////

// Adjust PC SFX acm name. Skip Underscore char at the start of PC App Name
static void __declspec(naked) FixPcSFX() {
	__asm {
		cmp byte ptr ds:[ebx], 0x5F; // check if Name begins with an '_' character
		jne endFunc;
		inc ebx;                     // shift address to next char
endFunc:
		// restore original code
		mov eax, ebx;
		cmp dword ptr ds:[FO_VAR_gsound_initialized], 0;
		retn;
	}
}

/*
// Set path to normal before printing or saving character details
static void __declspec(naked) FixCharScrnSaveNPrint() {
	__asm {
		push TempPathPtr //store current path
		mov  eax, _paths
		mov  TempPathPtr, eax //set path to normal
		push esi
		call OptionWindow_ //call char-scrn menu function
		pop  esi
		pop  TempPathPtr //restore stored path

		call RefreshPCArt
		ret
	}
}
*/

static void __declspec(naked) FixPcCriticalHitMsg() {
	__asm {
		cmp eax, critterListSize; // check if critter art in PC range
		jle endFunc;
		sub eax, critterListSize; // shift critter art index down out of hero range
endFunc:
		jmp fo::funcoffs::art_alias_num_;
	}
}

static const DWORD op_obj_art_fid_Ret = 0x45C5D9;
static void __declspec(naked) op_obj_art_fid_hack() {
	using namespace Fields;
	__asm {
		mov  esi, [edi + artFid];
		push ecx;
		call PartyControl::RealDudeObject;
		pop  ecx;
		cmp  eax, edi; // object is dude?
		jnz  skip;
		sub  esi, critterListSize; // fix hero FrmID
skip:
		jmp  op_obj_art_fid_Ret;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Load Appearance data from GCD file
static void __fastcall LoadGCDAppearance(fo::DbFile* fileStream) {
	currentRaceVal = 0;
	currentStyleVal = 0;
	DWORD temp;
	if (fo::func::db_freadInt(fileStream, &temp) != -1 && temp < 100) {
		currentRaceVal = (int)temp;
		if (fo::func::db_freadInt(fileStream, &temp) != -1 && temp < 100) {
			currentStyleVal = (int)temp;
		}
	}
	fo::func::db_fclose(fileStream);

	// load hero appearance
	if (LoadHeroDat(currentRaceVal, currentStyleVal, true) != 0) { // if load fails
		currentStyleVal = 0;                                       // set style to default
		if (LoadHeroDat(currentRaceVal, currentStyleVal) != 0) {   // if race fails with style at default
			currentRaceVal = 0;                                    // set race to default
			LoadHeroDat(currentRaceVal, currentStyleVal);
		}
	}
	fo::func::proto_dude_update_gender();
}

// Save Appearance data to GCD file
static void __fastcall SaveGCDAppearance(fo::DbFile *FileStream) {
	if (fo::func::db_fwriteInt(FileStream, (DWORD)currentRaceVal) != -1) {
		fo::func::db_fwriteInt(FileStream, (DWORD)currentStyleVal);
	}
	fo::func::db_fclose(FileStream);
}

static void EnableHeroAppearanceMod() {
	HeroAppearance::appModEnabled = true;

	// setup paths
	heroPathPtr = new fo::PathNode;
	racePathPtr = new fo::PathNode;
	heroPathPtr->path = new char[64];
	racePathPtr->path = new char[64];

	heroPathPtr->isDat = 0;
	racePathPtr->isDat = 0;
	heroPathPtr->pDat = nullptr;
	racePathPtr->pDat = nullptr;

	// Check if new Appearance char scrn button pushed (editor_design_)
	HookCall(0x431E9D, CheckCharScrnButtons);

	// Destroy new Appearance button mem after use (editor_design_)
	MakeCall(0x4329D8, CharScrnEnd, 1);

	// Check if sex has changed and reset char appearance (editor_design_)
	HookCall(0x4322E8, SexScrnEnd);

	// Load New Hero Art (xfopen_)
	MakeCall(0x4DEEE5, LoadNewHeroArt, 1);

	// Divert critter frm file name function exit for file checking (art_get_name_)
	SafeWrite8(0x419520, 0xEB); // divert func exit
	SafeWrite32(0x419521, 0x9090903E);

	// Check if new hero art exists otherwise use regular art (art_get_name_)
	MakeCall(0x419560, CheckHeroExist);

	// Double size of critter art index creating a new area for hero art (art_read_lst_)
	HookCall(0x4196B0, DoubleArt);

	// Add new hero critter names at end of critter list (art_init_)
	MakeCall(0x418B39, AddHeroCritNames);

	// Shift base hero critter art offset up into hero section (proto_dude_update_gender_)
	MakeCall(0x49F9DA, AdjustHeroBaseArt);

	// Adjust hero art index offset when changing armor (adjust_fid_)
	HookCall(0x4717D1, AdjustHeroArmorArt);

	// Hijack Save Hero State Structure fuction address 9CD54800
	// Return hero art index offset back to normal before saving
	SafeWrite32(0x519400, (DWORD)&SavCritNumFix);

	// Add new Appearance mod buttons (RegInfoAreas_)
	MakeCall(0x43A788, AddCharScrnButtons);

	// Mod char scrn background and add new app mod graphics. also adjust tag/skill button x pos (CharEditStart_)
	MakeCall(0x432B92, FixCharScrnBack);

	// Tag Skills text x pos
	SafeWrite32(0x433372, 0x24826 + 36);  // Tag Skills text x pos1
	SafeWrite32(0x4362BE, 0x24826 + 36);  // Tag Skills text x pos2
	SafeWrite32(0x4362F2, 522 + 36);      // Tag Skills num counter2 x pos1
	SafeWrite32(0x43631E, 522 + 36);      // Tag Skills num counter2 x pos2

	// skill points
	SafeWrite32(0x436262, 0x24810 + 36);  // Skill Points text x pos
	SafeWrite32(0x43628A, 522 + 36);      // Skill Points num counter x pos1
	SafeWrite32(0x43B5B2, 522 + 36);      // Skill Points num counter x pos2

	// make room for char view window
	SafeWrite32(0x433678, 347 + 76);      // shift skill buttons right 80
	SafeWrite32(0x4363CE, 380 + 68);      // shift skill name text right 80
	SafeWrite32(0x43641C, 573 + 10);      // shift skill % num text right 80
	SafeWrite32(0x43A74C, 223 - 76 + 10); // skill list mouse area button width
	SafeWrite32(0x43A75B, 370 + 76);      // skill list mouse area button xpos
	SafeWrite32(0x436220, 3580 + 68);     // skill text xpos
	SafeWrite32(0x43A71E, 223 - 68);      // skill button width
	SafeWrite32(0x43A72A, 376 + 68);      // skill button xpos

	// redraw area for skill list
	SafeWrite32(0x4361C4, 370 + 76); // xpos
	SafeWrite32(0x4361D9, 270 - 76); // width
	SafeWrite32(0x4361DE, 370 + 76); // xpos

	// skill slider thingy
	SafeWrite32(0x43647C, 592 + 3);  // xpos
	SafeWrite32(0x4364FA, 614 + 3);  // plus button xpos
	SafeWrite32(0x436567, 614 + 3);  // minus button xpos

	// fix for Char Screen note position was x484 y309 now x383 y308
	//SafeWrite32(0x43AB55, 308 * 640 + 483); // minus button xpos

	// Adjust PC SFX Name (gsound_load_sound_)
	MakeCall(0x4510EB, FixPcSFX, 2);

	// Set path to normal before printing or saving character details
	//HookCall(0x432359, FixCharScrnSaveNPrint);

	// Load Appearance data from GCD file (pc_load_data_)
	SafeWrite8(0x42DF5E, 0xF1); // mov ecx, esi "*FileStream"
	HookCall(0x42DF5F, LoadGCDAppearance);

	// Save Appearance data to GCD file (pc_save_data_)
	SafeWrite8(0x42E162, 0xF1); // mov ecx, esi "*FileStream"
	HookCall(0x42E163, SaveGCDAppearance);

	// Reset Appearance when selecting "Create Character" from the New Char screen (select_character_)
	MakeCall(0x4A7405, CreateCharReset);

	// Fixes missing console critical hit messages when PC is attacked. (combat_get_loc_name_)
	HookCall(0x42613A, FixPcCriticalHitMsg);

	// Force Criticals For Testing
	//SafeWrite32(0x423A8F, 0x90909090);
	//SafeWrite32(0x423A93, 0x90909090);
}

static void HeroAppearanceModExit() {
	if (!HeroAppearance::appModEnabled) return;

	if (heroPathPtr) {
		delete[] heroPathPtr->path;
		delete heroPathPtr;
	}
	if (racePathPtr) {
		delete[] racePathPtr->path;
		delete racePathPtr;
	}
}

void HeroAppearance::init() {
	int heroAppearanceMod = GetConfigInt("Misc", "EnableHeroAppearanceMod", 0);
	if (heroAppearanceMod > 0) {
		dlog("Setting up Appearance Char Screen buttons.", DL_INIT);
		EnableHeroAppearanceMod();
		// Hero FrmID fix for obj_art_fid script function
		if (heroAppearanceMod != 2) MakeJump(0x45C5C3, op_obj_art_fid_hack);

		LoadGameHook::OnAfterNewGame() += []() {
			SetNewCharAppearanceGlobals();
			LoadHeroAppearance();
		};
		Inventory::OnAdjustFid() += AdjustHeroArmorArt;
		dlogr(" Done", DL_INIT);
	}
}

void HeroAppearance::exit() {
	HeroAppearanceModExit();
}

}
