/*	
Copyright (C) 2006 Ricardo Pescuma Domenecci

This is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this file; see the file license.txt.  If
not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  
*/

// XXX
// All this file is one BIG HACK

#include "commons.h"



// Prototypes /////////////////////////////////////////////////////////////////////////////////////

HANDLE hOptHook = NULL;


static BOOL CALLBACK OptionsDlgProc(int type, HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static BOOL CALLBACK PopupsDlgProc(int type, HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);

#define HACK(i) \
		static BOOL CALLBACK OptionsDlgProc ## i (HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) \
		{ \
			return OptionsDlgProc(i, hwndDlg, msg, wParam, lParam); \
		} \
		static BOOL CALLBACK PopupsDlgProc ## i (HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) \
		{ \
			return PopupsDlgProc(i, hwndDlg, msg, wParam, lParam); \
		}

HACK(0);
HACK(1);
HACK(2);
HACK(3);

DLGPROC OptionsDlgProcArr[] = { OptionsDlgProc0, OptionsDlgProc1, OptionsDlgProc2, OptionsDlgProc3 };
DLGPROC PopupsDlgProcArr[] = { PopupsDlgProc0, PopupsDlgProc1, PopupsDlgProc2, PopupsDlgProc3 };


static BOOL MyAllowProtocol(const char *proto, void *param)
{
	return AllowProtocol((int) param, proto);
}

#define OPTIONS_CONTROLS_SIZE 2
static OptPageControl optionsControls[NUM_TYPES][OPTIONS_CONTROLS_SIZE];

#define POPUPS_CONTROLS_SIZE 16
static OptPageControl popupsControls[NUM_TYPES][POPUPS_CONTROLS_SIZE];

static UINT popupsExpertControls[] = { 
	IDC_COLOURS_G, IDC_BGCOLOR, IDC_BGCOLOR_L, IDC_TEXTCOLOR, IDC_TEXTCOLOR_L, IDC_WINCOLORS, IDC_DEFAULTCOLORS, 
	IDC_DELAY_G, IDC_DELAYFROMPU, IDC_DELAYCUSTOM, IDC_DELAYPERMANENT, IDC_DELAY, IDC_DELAY_SPIN,
	IDC_ACTIONS_G, IDC_RIGHT_ACTION_L, IDC_RIGHT_ACTION, IDC_LEFT_ACTION_L, IDC_LEFT_ACTION,
	IDC_PREV
};


Options opts[NUM_TYPES];


// Functions //////////////////////////////////////////////////////////////////////////////////////

#define SETTING(_X_) char _X_[128]; mir_snprintf(_X_, MAX_REGS(_X_), "%s_%s", types[type].name, setting)

DWORD GetSettingDword(int type, char *setting, DWORD def)
{
	SETTING(tmp);
	return DBGetContactSettingDword(NULL, MODULE_NAME, tmp, def);
}

WORD GetSettingWord(int type, char *setting, WORD def)
{
	SETTING(tmp);
	return DBGetContactSettingWord(NULL, MODULE_NAME, tmp, def);
}

BYTE GetSettingByte(int type, char *setting, BYTE def)
{
	SETTING(tmp);
	return DBGetContactSettingByte(NULL, MODULE_NAME, tmp, def);
}

BOOL GetSettingBool(int type, char *setting, BOOL def)
{
	SETTING(tmp);
	return DBGetContactSettingByte(NULL, MODULE_NAME, tmp, def) != 0;
}

void GetSettingTString(int type, char *setting, TCHAR *str, size_t str_size, TCHAR *def)
{
	DBVARIANT dbv;
	SETTING(tmp);
	if (!DBGetContactSettingTString(NULL, MODULE_NAME, tmp, &dbv))
	{
		lstrcpyn(str, dbv.ptszVal, str_size);
		DBFreeVariant(&dbv);
	}
	else
	{
		lstrcpyn(str, def, str_size);
	}
}

void WriteSettingDword(int type, char *setting, DWORD val)
{
	SETTING(tmp);
	DBWriteContactSettingDword(NULL, MODULE_NAME, tmp, val);
}

void WriteSettingWord(int type, char *setting, WORD val)
{
	SETTING(tmp);
	DBWriteContactSettingWord(NULL, MODULE_NAME, tmp, val);
}

void WriteSettingByte(int type, char *setting, BYTE val)
{
	SETTING(tmp);
	DBWriteContactSettingByte(NULL, MODULE_NAME, tmp, val);
}

void WriteSettingBool(int type, char *setting, BOOL val)
{
	SETTING(tmp);
	DBWriteContactSettingByte(NULL, MODULE_NAME, tmp, val ? 1 : 0);
}

void WriteSettingTString(int type, char *setting, TCHAR *str)
{
	SETTING(tmp);
	DBWriteContactSettingTString(NULL, MODULE_NAME, tmp, str);
}

int InitOptionsCallback(WPARAM wParam,LPARAM lParam)
{
	OPTIONSDIALOGPAGE odp;
	ZeroMemory(&odp,sizeof(odp));
	odp.cbSize=sizeof(odp);
	odp.position=0;
	odp.hInstance=hInst;
	odp.pszGroup = "History";
	odp.pszTemplate = MAKEINTRESOURCEA(IDD_OPTIONS);
	odp.flags = ODPF_BOLDGROUPS | ODPF_EXPERTONLY;

	for (int i = 0; i < NUM_TYPES; i++) 
	{
		odp.pszTitle = types[i].description;
		odp.pfnDlgProc = OptionsDlgProcArr[i];
		CallService(MS_OPT_ADDPAGE, wParam, (LPARAM)&odp);
	}


	if(ServiceExists(MS_POPUP_ADDPOPUPEX)
#ifdef UNICODE
		|| ServiceExists(MS_POPUP_ADDPOPUPW)
#endif
		)
	{
		ZeroMemory(&odp,sizeof(odp));
		odp.cbSize=sizeof(odp);
		odp.position=0;
		odp.hInstance=hInst;
		odp.pszGroup = Translate("Popups");
		odp.pszTemplate = MAKEINTRESOURCEA(IDD_POPUPS);
		odp.flags = ODPF_BOLDGROUPS;
		odp.expertOnlyControls = popupsExpertControls;
		odp.nExpertOnlyControls = MAX_REGS(popupsExpertControls);
		odp.nIDBottomSimpleControl = IDC_TRACK_G;

		for (int i = 0; i < NUM_TYPES; i++) 
		{
			char tmp[128];
			mir_snprintf(tmp, MAX_REGS(tmp), "%s Change", types[i].description);

			odp.pszTitle = tmp;
			odp.pfnDlgProc = PopupsDlgProcArr[i];
			CallService(MS_OPT_ADDPAGE,wParam,(LPARAM)&odp);
		}
	}

	return 0;
}


void InitOptions()
{
	static TCHAR changeTemplates[NUM_TYPES][128];
	static TCHAR removeTemplates[NUM_TYPES][128];
	static char optSet[NUM_TYPES][OPTIONS_CONTROLS_SIZE][64];
	static char popSet[NUM_TYPES][POPUPS_CONTROLS_SIZE][64];

	for (int i = 0; i < NUM_TYPES; i++) 
	{
		// Options page
		OptPageControl opt[] = {
			{ &opts[i].track_only_not_offline,	CONTROL_CHECKBOX,		IDC_ONLY_NOT_OFFLINE,	"TrackOnlyWhenNotOffline", types[i].defs.track_only_not_offline },
			{ NULL,								CONTROL_PROTOCOL_LIST,	IDC_PROTOCOLS,			"%sEnabled", TRUE, (int) MyAllowProtocol },
		};
		int j;
		for(j = 0; j < OPTIONS_CONTROLS_SIZE; j++)
		{
			mir_snprintf(&optSet[i][j][0], 64, "%s_%s", types[i].name, opt[j].setting);
			opt[j].setting = &optSet[i][j][0];
		}
		memcpy(&optionsControls[i][0], &opt, sizeof(opt));

		// Popups page
		TCHAR *tmp = mir_a2t(types[i].description);
		CharLower(tmp);

		mir_sntprintf(&changeTemplates[i][0], 128, TranslateT("changed his/her %s to %%new%% (was %%old%%)"), tmp);
		mir_sntprintf(&removeTemplates[i][0], 128, TranslateT("removed his/her %s (was %%old%%)"), tmp);

		mir_free(tmp);

		OptPageControl pops[] = {
			{ &opts[i].popup_track_changes,			CONTROL_CHECKBOX,	IDC_TRACK_CHANGE,	"PopupsTrackChanges", TRUE },
			{ &opts[i].popup_template_changed,		CONTROL_TEXT,		IDC_CHANGED,		"PopupsTemplateChanged", (DWORD) &changeTemplates[i][0] },
			{ &opts[i].popup_track_removes,			CONTROL_CHECKBOX,	IDC_TRACK_REMOVE,	"PopupsTrackRemoves", TRUE },
			{ &opts[i].popup_template_removed,		CONTROL_TEXT,		IDC_REMOVED,		"PopupsTemplateRemoved", (DWORD) &removeTemplates[i][0] },
			{ &opts[i].popup_dont_notfy_on_connect,	CONTROL_CHECKBOX,	IDC_DONT_NOTIFY_ON_CONNECT,	"PopupsDontNotifyOnConnect", types[i].defs.popup_dont_notfy_on_connect },
			{ &opts[i].popup_bkg_color,				CONTROL_COLOR,		IDC_BGCOLOR,		"PopupsBgColor", RGB(255,255,255) },
			{ &opts[i].popup_text_color,			CONTROL_COLOR,		IDC_TEXTCOLOR,		"PopupsTextColor", RGB(0,0,0) },
			{ &opts[i].popup_use_win_colors,		CONTROL_CHECKBOX,	IDC_WINCOLORS,		"PopupsWinColors", FALSE },
			{ &opts[i].popup_use_default_colors,	CONTROL_CHECKBOX,	IDC_DEFAULTCOLORS,	"PopupsDefaultColors", FALSE },
			{ &opts[i].popup_delay_type,			CONTROL_RADIO,		IDC_DELAYFROMPU,	"PopupsDelayType", POPUP_DELAY_DEFAULT, POPUP_DELAY_DEFAULT },
			{ NULL,									CONTROL_RADIO,		IDC_DELAYCUSTOM,	"PopupsDelayType", POPUP_DELAY_DEFAULT, POPUP_DELAY_CUSTOM },
			{ NULL,									CONTROL_RADIO,		IDC_DELAYPERMANENT,	"PopupsDelayType", POPUP_DELAY_DEFAULT, POPUP_DELAY_PERMANENT },
			{ &opts[i].popup_timeout,				CONTROL_SPIN,		IDC_DELAY,			"PopupsTimeout", 10, IDC_DELAY_SPIN, (WORD) 1, (WORD) 255 },
			{ &opts[i].popup_right_click_action,	CONTROL_COMBO,		IDC_RIGHT_ACTION,	"PopupsRightClick", POPUP_ACTION_CLOSEPOPUP },
			{ &opts[i].popup_left_click_action,		CONTROL_COMBO,		IDC_LEFT_ACTION,	"PopupsLeftClick", POPUP_ACTION_OPENHISTORY }
		};
		for(j = 0; j < POPUPS_CONTROLS_SIZE; j++)
		{
			mir_snprintf(&popSet[i][j][0], 64, "%s_%s", types[i].name, pops[j].setting);
			pops[j].setting = &popSet[i][j][0];
		}
		memcpy(&popupsControls[i][0], &pops, sizeof(pops));
	}

	LoadOptions();

	hOptHook = HookEvent(ME_OPT_INITIALISE, InitOptionsCallback);
}


void DeInitOptions()
{
	UnhookEvent(hOptHook);
}


void LoadOptions()
{
	for (int i = 0; i < NUM_TYPES; i++) 
	{
		LoadOpts(&optionsControls[i][0], OPTIONS_CONTROLS_SIZE, MODULE_NAME);
		LoadOpts(&popupsControls[i][0], POPUPS_CONTROLS_SIZE, MODULE_NAME);
	}
}


static BOOL CALLBACK OptionsDlgProc(int type, HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) 
{
	return SaveOptsDlgProc(&optionsControls[type][0], OPTIONS_CONTROLS_SIZE, MODULE_NAME, hwndDlg, msg, wParam, lParam);
}


static void PopupsEnableDisableCtrls(HWND hwndDlg)
{
	EnableWindow(GetDlgItem(hwndDlg, IDC_CHANGED_L), IsDlgButtonChecked(hwndDlg, IDC_TRACK_CHANGE));
	EnableWindow(GetDlgItem(hwndDlg, IDC_CHANGED), IsDlgButtonChecked(hwndDlg, IDC_TRACK_CHANGE));

	EnableWindow(GetDlgItem(hwndDlg, IDC_REMOVED_L), IsDlgButtonChecked(hwndDlg, IDC_TRACK_REMOVE));
	EnableWindow(GetDlgItem(hwndDlg, IDC_REMOVED), IsDlgButtonChecked(hwndDlg, IDC_TRACK_REMOVE));
	
	EnableWindow(GetDlgItem(hwndDlg, IDC_BGCOLOR), 
			!IsDlgButtonChecked(hwndDlg, IDC_WINCOLORS) && 
			!IsDlgButtonChecked(hwndDlg, IDC_DEFAULTCOLORS));
	EnableWindow(GetDlgItem(hwndDlg, IDC_TEXTCOLOR), 
			!IsDlgButtonChecked(hwndDlg, IDC_WINCOLORS) && 
			!IsDlgButtonChecked(hwndDlg, IDC_DEFAULTCOLORS));
	EnableWindow(GetDlgItem(hwndDlg, IDC_DEFAULTCOLORS), 
			!IsDlgButtonChecked(hwndDlg, IDC_WINCOLORS));
	EnableWindow(GetDlgItem(hwndDlg, IDC_WINCOLORS), 
			!IsDlgButtonChecked(hwndDlg, IDC_DEFAULTCOLORS));

	EnableWindow(GetDlgItem(hwndDlg, IDC_DELAY), 
			IsDlgButtonChecked(hwndDlg, IDC_DELAYCUSTOM));
}


static BOOL CALLBACK PopupsDlgProc(int type, HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) 
{
	switch (msg) 
	{
		case WM_INITDIALOG:
		{
			TCHAR *desc = mir_a2t(types[type].description);
			CharLower(desc);

			TCHAR tmp[256];
			mir_sntprintf(tmp, MAX_REGS(tmp), TranslateT("Show when contacts change their %s"), desc);
			SetDlgItemText(hwndDlg, IDC_TRACK_CHANGE, tmp);
			mir_sntprintf(tmp, MAX_REGS(tmp), TranslateT("Show when contacts remove their %s"), desc);
			SetDlgItemText(hwndDlg, IDC_TRACK_REMOVE, tmp);

			mir_free(desc);

			SendDlgItemMessage(hwndDlg, IDC_RIGHT_ACTION, CB_ADDSTRING, 0, (LONG) TranslateT("Do nothing"));
			SendDlgItemMessage(hwndDlg, IDC_RIGHT_ACTION, CB_ADDSTRING, 0, (LONG) TranslateT("Close popup"));
			SendDlgItemMessage(hwndDlg, IDC_RIGHT_ACTION, CB_ADDSTRING, 0, (LONG) TranslateT("Show history"));

			SendDlgItemMessage(hwndDlg, IDC_LEFT_ACTION, CB_ADDSTRING, 0, (LONG) TranslateT("Do nothing"));
			SendDlgItemMessage(hwndDlg, IDC_LEFT_ACTION, CB_ADDSTRING, 0, (LONG) TranslateT("Close popup"));
			SendDlgItemMessage(hwndDlg, IDC_LEFT_ACTION, CB_ADDSTRING, 0, (LONG) TranslateT("Show history"));

			// Needs to be called here in this case
			BOOL ret = SaveOptsDlgProc(&popupsControls[type][0], POPUPS_CONTROLS_SIZE, MODULE_NAME, hwndDlg, msg, wParam, lParam);

			PopupsEnableDisableCtrls(hwndDlg);

			return ret;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam)) 
			{
				case IDC_TRACK_REMOVE:
				case IDC_TRACK_CHANGE:
				case IDC_POPUPS:
				case IDC_WINCOLORS:
				case IDC_DEFAULTCOLORS:
				case IDC_DELAYFROMPU:
				case IDC_DELAYPERMANENT:
				case IDC_DELAYCUSTOM:
				{
					if (HIWORD(wParam) == BN_CLICKED)
						PopupsEnableDisableCtrls(hwndDlg);

					break;
				}
				case IDC_PREV: 
				{
					Options op;

					if (IsDlgButtonChecked(hwndDlg, IDC_DELAYFROMPU))
						op.popup_delay_type = POPUP_DELAY_DEFAULT;
					else if (IsDlgButtonChecked(hwndDlg, IDC_DELAYCUSTOM))
						op.popup_delay_type = POPUP_DELAY_CUSTOM;
					else if (IsDlgButtonChecked(hwndDlg, IDC_DELAYPERMANENT))
						op.popup_delay_type = POPUP_DELAY_PERMANENT;

					op.popup_timeout = GetDlgItemInt(hwndDlg,IDC_DELAY, NULL, FALSE);
					op.popup_bkg_color = SendDlgItemMessage(hwndDlg,IDC_BGCOLOR,CPM_GETCOLOUR,0,0);
					op.popup_text_color = SendDlgItemMessage(hwndDlg,IDC_TEXTCOLOR,CPM_GETCOLOUR,0,0);
					op.popup_use_win_colors = IsDlgButtonChecked(hwndDlg, IDC_WINCOLORS) != 0;
					op.popup_use_default_colors = IsDlgButtonChecked(hwndDlg, IDC_DEFAULTCOLORS) != 0;

					ShowTestPopup(type, TranslateT("Test Contact"), TranslateT("Test description"), &op);

					break;
				}
			}
			break;
		}
	}

	return SaveOptsDlgProc(&popupsControls[type][0], POPUPS_CONTROLS_SIZE, MODULE_NAME, hwndDlg, msg, wParam, lParam);
}
