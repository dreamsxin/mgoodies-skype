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

#include "commons.h"


// Prototypes ///////////////////////////////////////////////////////////////////////////


PLUGININFOEX pluginInfo={
	sizeof(PLUGININFOEX),
#ifdef UNICODE
	"Spell Checker (Unicode)",
#else
	"Spell Checker",
#endif
	PLUGIN_MAKE_VERSION(0,0,3,0),
	"Spell Checker",
	"Ricardo Pescuma Domenecci",
	"",
	"� 2006 Ricardo Pescuma Domenecci",
	"http://pescuma.mirandaim.ru/miranda/spellchecker",
	UNICODE_AWARE,
	0,		//doesn't replace anything built-in
#ifdef UNICODE
	{ 0x36753ae3, 0x840b, 0x4797, { 0x94, 0xa5, 0xfd, 0x9f, 0x58, 0x52, 0xb9, 0x42 } } // {36753AE3-840B-4797-94A5-FD9F5852B942}
#else
	{ 0x3cdbcd92, 0x94d7, 0x466a, { 0x94, 0x7f, 0x4e, 0xe0, 0x1c, 0xca, 0xf4, 0x89 } } // {3CDBCD92-94D7-466a-947F-4EE01CCAF489}
#endif
};

typedef struct
{
	TCHAR* szDescr;
	char* szName;
	int   defIconID;
} IconStruct;

static IconStruct iconList[] =
{
	{  _T("Enabled"),       "spellchecker_enabled",       IDI_CHECK         },
	{  _T("Disabled"),      "spellchecker_disabled",      IDI_NO_CHECK      },
//	{  _T("Unknown Flag"),  "spellchecker_unknown_flag",  IDI_UNKNOWN_FLAG  },
};

#define TIMER_ID 17982
#define WMU_DICT_CHANGED (WM_USER+1)
#define WMU_KBDL_CHANGED (WM_USER+2)

HINSTANCE hInst;
PLUGINLINK *pluginLink;

HANDLE hHooks[6];
HANDLE hServices[3];

HANDLE hDictionariesFolder = NULL;
TCHAR dictionariesFolder[1024];

HANDLE hCustomDictionariesFolder = NULL;
TCHAR customDictionariesFolder[1024];

HANDLE hFlagsDllFolder = NULL;
TCHAR flagsDllFolder[1024];

HBITMAP hCheckedBmp;
BITMAP bmpChecked;

char *metacontacts_proto = NULL;
BOOL loaded = FALSE;

Dictionaries languages = {0};

typedef map<HWND, Dialog *> DialogMapType;

DialogMapType dialogs;
DialogMapType menus;

int ModulesLoaded(WPARAM wParam, LPARAM lParam);
int PreShutdown(WPARAM wParam, LPARAM lParam);
int MsgWindowEvent(WPARAM wParam, LPARAM lParam);
int MsgWindowPopup(WPARAM wParam, LPARAM lParam);
int IconsChanged(WPARAM wParam, LPARAM lParam);
int IconPressed(WPARAM wParam, LPARAM lParam);

int AddContactTextBox(HANDLE hContact, HWND hwnd, char *name, BOOL srmm, HWND hwndOwner);
int RemoveContactTextBox(HWND hwnd);
int ShowPopupMenu(HWND hwnd, HMENU hMenu, POINT pt, HWND hwndOwner);

void AddReplaceDialog(Dictionary *dict, TCHAR *word, HWND hwndParent = NULL) ;

int AddContactTextBoxService(WPARAM wParam, LPARAM lParam);
int RemoveContactTextBoxService(WPARAM wParam, LPARAM lParam);
int ShowPopupMenuService(WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ModifyIcon(Dialog *dlg);
BOOL GetWordCharRange(Dialog *dlg, CHARRANGE &sel, TCHAR *text, size_t text_len, int &first_char);
TCHAR *GetWordUnderPoint(Dialog *dlg, POINT pt, CHARRANGE &sel);

int GetClosestLanguage(TCHAR *lang_name);

typedef void (*FoundWrongWordCallback)(TCHAR *word, CHARRANGE pos, void *param);


#define DEFINE_GUIDXXX(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        const GUID CDECL name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUIDXXX(IID_ITextDocument,0x8CC497C0,0xA1DF,0x11CE,0x80,0x98,
                0x00,0xAA,0x00,0x47,0xBE,0x5D);

#define SUSPEND_UNDO(dlg)														\
	if (dlg->textDocument != NULL)												\
		dlg->textDocument->Undo(tomSuspend, NULL)

#define RESUME_UNDO(dlg)														\
	if (dlg->textDocument != NULL)												\
		dlg->textDocument->Undo(tomResume, NULL)

#define	STOP_RICHEDIT(dlg)														\
	SUSPEND_UNDO(dlg);															\
	SendMessage(dlg->hwnd, WM_SETREDRAW, FALSE, 0);								\
	POINT __old_scroll_pos;														\
	SendMessage(dlg->hwnd, EM_GETSCROLLPOS, 0, (LPARAM) &__old_scroll_pos);		\
	CHARRANGE __old_sel;														\
	SendMessage(dlg->hwnd, EM_EXGETSEL, 0, (LPARAM) &__old_sel);				\
	POINT __caretPos;															\
	GetCaretPos(&__caretPos);													\
    DWORD __old_mask = SendMessage(dlg->hwnd, EM_GETEVENTMASK, 0, 0);			\
	SendMessage(dlg->hwnd, EM_SETEVENTMASK, 0, __old_mask & ~ENM_CHANGE);		\
	BOOL __inverse = (__old_sel.cpMin >= LOWORD(SendMessage(dlg->hwnd, EM_CHARFROMPOS, 0, (LPARAM) &__caretPos)))

#define START_RICHEDIT(dlg)														\
	if (__inverse)																\
	{																			\
		LONG __tmp = __old_sel.cpMin;											\
		__old_sel.cpMin = __old_sel.cpMax;										\
		__old_sel.cpMax = __tmp;												\
	}																			\
	SendMessage(dlg->hwnd, EM_SETEVENTMASK, 0, __old_mask);						\
	SendMessage(dlg->hwnd, EM_EXSETSEL, 0, (LPARAM) &__old_sel);				\
	SendMessage(dlg->hwnd, EM_SETSCROLLPOS, 0, (LPARAM) &__old_scroll_pos);		\
	SendMessage(dlg->hwnd, WM_SETREDRAW, TRUE, 0);								\
	InvalidateRect(dlg->hwnd, NULL, FALSE);										\
	RESUME_UNDO(dlg)


// Functions ////////////////////////////////////////////////////////////////////////////


int mlog(const char *function, const char *fmt, ...)
{
#if 0
    va_list va;
    char text[1024];
	size_t len;
	static DWORD tick = GetTickCount();

	mir_snprintf(text, sizeof(text) - 10, "[%08u - %08u] [%8u] [%s] ", 
				 GetCurrentThreadId(), GetTickCount(), GetTickCount() - tick, function);
	tick = GetTickCount();
	len = strlen(text);

    va_start(va, fmt);
    mir_vsnprintf(&text[len], sizeof(text) - len, fmt, va);
    va_end(va);

	FILE *fp = fopen("c:\\miranda_spellchecker.log.txt","at");

	if (fp != NULL)
	{
		fprintf(fp, "%s\n", text);
		fclose(fp);
		return 0;
	}
	else
	{
		return -1;
	}
#else
	return 0;
#endif
}


HICON LoadIconEx(Dictionary *dict, BOOL copy)
{
#ifdef UNICODE
	char lang[32];
	WideCharToMultiByte(CP_ACP, 0, dict->language, -1, lang, sizeof(lang), NULL, NULL);
	return LoadIconEx(lang, copy);
#else
	return LoadIconEx(dict->language, copy);
#endif
}


HICON LoadIconEx(char *iconName, BOOL copy)
{
	HICON hIcon = hIcon = (HICON)CallService(MS_SKIN2_GETICON, 0, (LPARAM)iconName);
	if (copy)
	{
		hIcon = CopyIcon(hIcon);
		CallService(MS_SKIN2_RELEASEICON, 0, (LPARAM)iconName);
	}
	return hIcon;
}


void ReleaseIconEx(HICON hIcon)
{
	CallService(MS_SKIN2_RELEASEICON, (WPARAM)hIcon, 0);
}



extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) 
{
	hInst = hinstDLL;
	return TRUE;
}


extern "C" __declspec(dllexport) PLUGININFO* MirandaPluginInfo(DWORD mirandaVersion) 
{
	pluginInfo.cbSize = sizeof(PLUGININFO);
	return (PLUGININFO*) &pluginInfo;
}


extern "C" __declspec(dllexport) PLUGININFOEX* MirandaPluginInfoEx(DWORD mirandaVersion)
{
	pluginInfo.cbSize = sizeof(PLUGININFOEX);
	return &pluginInfo;
}


static const MUUID interfaces[] = { MIID_SPELLCHECKER, MIID_LAST };
extern "C" __declspec(dllexport) const MUUID* MirandaPluginInterfaces(void)
{
	return interfaces;
}


extern "C" int __declspec(dllexport) Load(PLUGINLINK *link) 
{
	pluginLink = link;

	init_mir_malloc();

	// hooks
	hHooks[0] = HookEvent(ME_SYSTEM_MODULESLOADED, ModulesLoaded);
	hHooks[1] = HookEvent(ME_SYSTEM_PRESHUTDOWN, PreShutdown);

	hCheckedBmp = LoadBitmap(NULL, MAKEINTRESOURCE(OBM_CHECK));
	if (GetObject(hCheckedBmp, sizeof(bmpChecked), &bmpChecked) == 0)
		bmpChecked.bmHeight = bmpChecked.bmWidth = 10;

	return 0;
}

extern "C" int __declspec(dllexport) Unload(void) 
{
	DeleteObject(hCheckedBmp);

	for(unsigned i = 0; i < MAX_REGS(hServices); ++i)
		DestroyServiceFunction(hServices[i]);

	return 0;
}

// Called when all the modules are loaded
int ModulesLoaded(WPARAM wParam, LPARAM lParam) 
{
	if (ServiceExists(MS_MC_GETPROTOCOLNAME))
		metacontacts_proto = (char *) CallService(MS_MC_GETPROTOCOLNAME, 0, 0);

	// add our modules to the KnownModules list
	CallService("DBEditorpp/RegisterSingleModule", (WPARAM) MODULE_NAME, 0);

	TCHAR mirandaFolder[1024];
	GetModuleFileName(GetModuleHandle(NULL), mirandaFolder, MAX_REGS(mirandaFolder));
	TCHAR *p = _tcsrchr(mirandaFolder, _T('\\'));
	if (p != NULL)
		*p = _T('\0');

    // updater plugin support
    if(ServiceExists(MS_UPDATE_REGISTER))
	{
		Update upd = {0};
		char szCurrentVersion[30];

		upd.cbSize = sizeof(upd);
		upd.szComponentName = pluginInfo.shortName;

		upd.szUpdateURL = UPDATER_AUTOREGISTER;

		upd.szBetaVersionURL = "http://pescuma.mirandaim.ru/miranda/spellchecker_version.txt";
		upd.szBetaChangelogURL = "http://pescuma.mirandaim.ru/miranda/spellchecker#Changelog";
		upd.pbBetaVersionPrefix = (BYTE *)"Spell Checker ";
		upd.cpbBetaVersionPrefix = strlen((char *)upd.pbBetaVersionPrefix);
#ifdef UNICODE
		upd.szBetaUpdateURL = "http://pescuma.mirandaim.ru/miranda/spellcheckerW.zip";
#else
		upd.szBetaUpdateURL = "http://pescuma.mirandaim.ru/miranda/spellchecker.zip";
#endif

		upd.pbVersion = (BYTE *)CreateVersionStringPlugin((PLUGININFO*) &pluginInfo, szCurrentVersion);
		upd.cpbVersion = strlen((char *)upd.pbVersion);

        CallService(MS_UPDATE_REGISTER, 0, (LPARAM)&upd);
	}

    // Folders plugin support
	if (ServiceExists(MS_FOLDERS_REGISTER_PATH))
	{
		hDictionariesFolder = (HANDLE) FoldersRegisterCustomPathT(Translate("Spell Checker"), 
					Translate("Dictionaries"), 
					_T(MIRANDA_PATH) _T("\\Dictionaries"));

		FoldersGetCustomPathT(hDictionariesFolder, dictionariesFolder, MAX_REGS(dictionariesFolder), _T("."));

		hCustomDictionariesFolder = (HANDLE) FoldersRegisterCustomPathT(Translate("Spell Checker"), 
					Translate("Custom Dictionaries"), 
					_T(PROFILE_PATH) _T("\\") _T(CURRENT_PROFILE) _T("\\Dictionaries"));

		FoldersGetCustomPathT(hCustomDictionariesFolder, customDictionariesFolder, MAX_REGS(customDictionariesFolder), _T("."));
		
		hFlagsDllFolder = (HANDLE) FoldersRegisterCustomPathT(Translate("Spell Checker"), 
					Translate("Flags DLL"), 
					_T(MIRANDA_PATH) _T("\\Icons"));

		FoldersGetCustomPathT(hFlagsDllFolder, flagsDllFolder, MAX_REGS(flagsDllFolder), _T("."));
	}
	else
	{
		mir_sntprintf(dictionariesFolder, MAX_REGS(dictionariesFolder), _T("%s\\Dictionaries"), mirandaFolder);

		char profileFolder[1024];
		CallService(MS_DB_GETPROFILEPATH, (WPARAM) MAX_REGS(customDictionariesFolder), (LPARAM) profileFolder);

#ifdef UNICODE
		mir_sntprintf(customDictionariesFolder, MAX_REGS(customDictionariesFolder), _T("%S\\Dictionaries"), profileFolder);
#else
		mir_sntprintf(customDictionariesFolder, MAX_REGS(customDictionariesFolder), _T("%s\\Dictionaries"), profileFolder);
#endif

		mir_sntprintf(flagsDllFolder, MAX_REGS(flagsDllFolder), _T("%s\\Icons"), mirandaFolder);
	}

	char path[1024];
	GetModuleFileNameA(hInst, path, MAX_REGS(path));

	SKINICONDESC sid = {0};
	sid.cbSize = sizeof(SKINICONDESC);
	sid.flags = SIDF_TCHAR;
	sid.ptszSection = TranslateT("Spell Checker");
	sid.pszDefaultFile = path;

	for (unsigned i = 0; i < MAX_REGS(iconList); ++i)
	{
		sid.ptszDescription = TranslateTS(iconList[i].szDescr);
		sid.pszName = iconList[i].szName;
		sid.iDefaultIndex = -iconList[i].defIconID;
		CallService(MS_SKIN2_ADDICON, 0, (LPARAM)&sid);
	}

	languages = GetAvaibleDictionaries(dictionariesFolder, customDictionariesFolder);

	InitOptions();

	if (opts.use_flags)
	{
		// Load flags dll
		TCHAR flag_file[1024];
		mir_sntprintf(flag_file, MAX_REGS(flag_file), _T("%s\\flags.dll"), flagsDllFolder);
		HMODULE hFlagsDll = LoadLibrary(flag_file);

		sid.flags = SIDF_TCHAR | SIDF_SORTED;
		sid.ptszSection = TranslateT("Spell Checker/Flags");

		// Get language flags
		for(unsigned i = 0; i < languages.count; i++)
		{
			sid.ptszDescription = languages.dicts[i]->full_name;
#ifdef UNICODE
			char lang[32];
			mir_snprintf(lang, MAX_REGS(lang), "%S", languages.dicts[i]->language);
			sid.pszName = lang;
#else
			sid.pszName = languages.dicts[i]->language;
#endif

			HICON hFlag;
			if (hFlagsDll != NULL)
				hFlag = (HICON) LoadImage(hFlagsDll, languages.dicts[i]->language, IMAGE_ICON, 16, 16, 0);
			else
				hFlag = NULL;

			if (hFlag != NULL)
			{
				sid.hDefaultIcon = hFlag;
				sid.pszDefaultFile = NULL;
				sid.iDefaultIndex = 0;
			}
			else
			{
				sid.hDefaultIcon = NULL;
				sid.pszDefaultFile = path;
				sid.iDefaultIndex = - IDI_UNKNOWN_FLAG;
			}

			// Oki, lets add to IcoLib, then
			CallService(MS_SKIN2_ADDICON, 0, (LPARAM)&sid);
			
			if (hFlag != NULL)
				DestroyIcon(hFlag);
		}
		FreeLibrary(hFlagsDll);
	}

	if (opts.default_language[0] != _T('\0'))
	{
		for(unsigned i = 0; i < languages.count; i++)
		{
			if (lstrcmp(languages.dicts[i]->language, opts.default_language) == 0)
			{
				languages.dicts[i]->load();
				break;
			}
		}
	}

	hHooks[2] = HookEvent(ME_SKIN2_ICONSCHANGED, &IconsChanged);
	hHooks[3] = HookEvent(ME_MSG_WINDOWEVENT, &MsgWindowEvent);
	hHooks[4] = HookEvent(ME_MSG_WINDOWPOPUP, &MsgWindowPopup);
	hHooks[5] = HookEvent(ME_MSG_ICONPRESSED, &IconPressed);

	hServices[0] = CreateServiceFunction(MS_SPELLCHECKER_ADD_RICHEDIT, AddContactTextBoxService);
	hServices[1] = CreateServiceFunction(MS_SPELLCHECKER_REMOVE_RICHEDIT, RemoveContactTextBoxService);
	hServices[2] = CreateServiceFunction(MS_SPELLCHECKER_SHOW_POPUP_MENU, ShowPopupMenuService);

	if (ServiceExists(MS_MSG_ADDICON))
	{
		StatusIconData sid = {0};
		sid.cbSize = sizeof(sid);
		sid.szModule = MODULE_NAME;
		sid.hIconDisabled = LoadIconEx("spellchecker_disabled", TRUE);
		sid.flags = MBF_HIDDEN;

		for(unsigned i = 0; i < languages.count; i++)
		{
			sid.dwId = i;

			char tmp[128];
			mir_snprintf(tmp, MAX_REGS(tmp), "%s - " TCHAR_STR_PARAM, 
				Translate("Spell Checker"), languages.dicts[i]->full_name);
			sid.szTooltip = tmp;

			if (opts.use_flags)
				sid.hIcon = LoadIconEx(languages.dicts[i], TRUE);
			else
				sid.hIcon = LoadIconEx("spellchecker_enabled", TRUE);

			CallService(MS_MSG_ADDICON, 0, (LPARAM) &sid);
		}
	}

	loaded = TRUE;

	return 0;
}


int IconsChanged(WPARAM wParam, LPARAM lParam) 
{
	if (ServiceExists(MS_MSG_MODIFYICON))
	{
		StatusIconData sid = {0};
		sid.cbSize = sizeof(sid);
		sid.szModule = MODULE_NAME;
		sid.hIconDisabled = LoadIconEx("spellchecker_disabled", TRUE);
		sid.flags = MBF_HIDDEN;
		

		for(unsigned i = 0; i < languages.count; i++)
		{
			sid.dwId = i;

			char tmp[128];
			mir_snprintf(tmp, MAX_REGS(tmp), "%s - " TCHAR_STR_PARAM, 
				Translate("Spell Checker"), languages.dicts[i]->full_name);
			sid.szTooltip = tmp;

			if (opts.use_flags)
				sid.hIcon = LoadIconEx(languages.dicts[i], TRUE);
			else
				sid.hIcon = LoadIconEx("spellchecker_enabled", TRUE);

			CallService(MS_MSG_MODIFYICON, 0, (LPARAM) &sid);
		}
	}

	return 0;
}


int PreShutdown(WPARAM wParam, LPARAM lParam)
{
	DeInitOptions();

	if (ServiceExists(MS_MSG_REMOVEICON))
	{
		StatusIconData sid = {0};
		sid.cbSize = sizeof(sid);
		sid.szModule = MODULE_NAME;
		CallService(MS_MSG_REMOVEICON, 0, (LPARAM) &sid);
	}

	for(unsigned i=0; i<MAX_REGS(hHooks); ++i)
		UnhookEvent(hHooks[i]);

	return 0;
}


void SetUnderline(HWND hRichEdit, int pos_start, int pos_end, BOOL all = FALSE, BOOL disable = FALSE)
{
	if (!all)
	{
		// Select this range
		CHARRANGE sel = { pos_start, pos_end };
		SendMessage(hRichEdit, EM_EXSETSEL, 0, (LPARAM) &sel);
	}

	CHARFORMAT2 cf;
	cf.cbSize = sizeof(CHARFORMAT2);
	cf.dwMask = CFM_UNDERLINE | CFM_UNDERLINETYPE;
	cf.dwEffects = disable ? 0 : CFE_UNDERLINE;
	cf.bUnderlineType = disable ? 0 : ((opts.underline_type + CFU_UNDERLINEDOUBLE) | 0x50);
	SendMessage(hRichEdit, EM_SETCHARFORMAT, (WPARAM) all ? SCF_ALL : SCF_SELECTION, (LPARAM)&cf);
}


void SetNoUnderline(HWND hRichEdit, int pos_start, int pos_end)
{
	SetUnderline(hRichEdit, pos_start, pos_end, FALSE, TRUE);
}

void SetNoUnderline(HWND hRichEdit)
{
	SetUnderline(hRichEdit, 0, 0, TRUE, TRUE);
}


inline void GetLineOfText(Dialog *dlg, int line, int &first_char, TCHAR *text, size_t text_len)
{
	first_char = SendMessage(dlg->hwnd, EM_LINEINDEX, (WPARAM) line, 0);

	*((WORD*)text) = text_len - 1;
	int size = SendMessage(dlg->hwnd, EM_GETLINE, (WPARAM) line, (LPARAM) text);
	size = max(0, min(text_len-1, size-1));
	text[size] = _T('\0');
}

// Helper to avoid copy and pastle
inline void DealWord(Dialog *dlg, TCHAR *text, int &first_char, int &last_pos, int &pos, 
					 CHARRANGE &old_sel, int &diff,
					 FoundWrongWordCallback callback, void *param)
{
	text[pos] = _T('\0');

	// Is upper?
	if (opts.ignore_uppercase)
	{
		BOOL upper = TRUE;
		for(int i = last_pos; i < pos && upper; i++)
			upper = IsCharUpper(text[i]);

		if (upper)
			return;
	}

	// Is wrong?
	if (!dlg->lang->spell(&text[last_pos]))
	{
		CHARRANGE sel = { first_char + last_pos + diff, first_char + pos + diff };
		BOOL mark = TRUE;

		// Has to correct?
		if (opts.auto_replace_dict || opts.auto_replace_user)
		{
			TCHAR *word = NULL;

			if (opts.auto_replace_user)
				word = dlg->lang->autoReplace(&text[last_pos]);

			if (opts.auto_replace_dict && word == NULL)
				word = dlg->lang->autoSuggestOne(&text[last_pos]);

			if (word != NULL)
			{
				mark = FALSE;

				// Replace in rich edit
				SendMessage(dlg->hwnd, EM_EXSETSEL, 0, (LPARAM) &sel);

				RESUME_UNDO(dlg);

				SendMessage(dlg->hwnd, EM_REPLACESEL, TRUE, (LPARAM) word);

				SUSPEND_UNDO(dlg);

				// Fix old sel
				int dif = lstrlen(word) - sel.cpMax + sel.cpMin;
				if (old_sel.cpMin >= sel.cpMax)
					old_sel.cpMin += dif;
				if (old_sel.cpMax >= sel.cpMax)
					old_sel.cpMax += dif;
				diff += dif;

				free(word);
			}
		}
		
		// Mark
		if (mark)
		{
			SetUnderline(dlg->hwnd, sel.cpMin, sel.cpMax);
				
			if (callback != NULL)
				callback(&text[last_pos], sel, param);
		}
	}
}


// Checks for errors in all text
void CheckText(Dialog *dlg, BOOL check_word_under_cursor, 
			   FoundWrongWordCallback callback = NULL, void *param = NULL)
{
	STOP_RICHEDIT(dlg);

	if (GetWindowTextLength(dlg->hwnd) > 0)
	{
		int lines = SendMessage(dlg->hwnd, EM_GETLINECOUNT, 0, 0);
		int line = 0;

		if (!check_word_under_cursor)
		{
			// Check only the current line, one up and one down
			int current_line = SendMessage(dlg->hwnd, EM_LINEFROMCHAR, (WPARAM) __old_sel.cpMin, 0);
			line = max(line, current_line - 1);
			lines = min(lines, current_line + 2);
		}

		for(; line < lines; line++) 
		{
			TCHAR text[1024];
			int first_char;
			int diff = 0;

			GetLineOfText(dlg, line, first_char, text, MAX_REGS(text));
			int len = lstrlen(text);

			SetNoUnderline(dlg->hwnd, first_char, first_char + len);

			// Now lets get the words
			int last_pos = -1;
			BOOL found_real_char = FALSE;
			for (int pos = 0; pos < len; pos++)
			{
				if (!dlg->lang->isWordChar(text[pos]) && !(text[pos] >= _T('0') && text[pos] <= _T('9')))
				{
					if (last_pos != -1)
					{
						// We found a word

						// Is this an URL?
						if (found_real_char 
							&& text[pos] == _T(':') && text[pos+1] == _T('/') && text[pos+2] == _T('/')
							&& pos - last_pos >= 3 && pos - last_pos <=4) 
						{
							// May be, lets check
							int p = last_pos;
							for(;  (text[p] >= _T('a') && text[p] <= _T('z'))
								|| (text[p] >= _T('A') && text[p] <= _T('Z'))
								|| (text[p] >= _T('0') && text[p] <= _T('9'))
								|| text[p] == _T('.') || text[p] == _T('/')
								|| text[p] == _T('\\') || text[p] == _T('?')
								|| text[p] == _T('=') || text[p] == _T('&')
								|| text[p] == _T('%') || text[p] == _T('-')
								|| text[p] == _T('_')|| text[p] == _T(':'); p++) 
							{}

							if (p > pos)
							{
								// Found ya
								pos = p;
								found_real_char = FALSE;
							}
						}

						// Or at least a site?
						if (found_real_char && text[pos] == _T('.')) {
							// Let's see if fits in the description
							int p = last_pos;
							int num_ids = 0;
							for(;  (text[p] >= _T('a') && text[p] <= _T('z'))
								|| (text[p] >= _T('A') && text[p] <= _T('Z'))
								|| (text[p] >= _T('0') && text[p] <= _T('9'))
								|| text[p] == _T('.') || text[p] == _T('/')
								|| text[p] == _T('\\') || text[p] == _T('?')
								|| text[p] == _T('=') || text[p] == _T('&')
								|| text[p] == _T('%') || text[p] == _T('-')
								|| text[p] == _T('_'); p++) 
							{
								if (text[p] == _T('.') || text[p] == _T('/'))
									num_ids++;
							}
							
							if (p > pos && num_ids >= 2)
							{
								// Found ya
								pos = p;
								found_real_char = FALSE;
							}
						}

						// Is this an email?
						if (found_real_char && text[pos] == _T('@')) {
							// Well, it is
							// It is! So lets found where it ends
							pos ++;
							for(;  (text[pos] >= _T('a') && text[pos] <= _T('z'))
								|| (text[pos] >= _T('A') && text[pos] <= _T('Z'))
								|| (text[pos] >= _T('0') && text[pos] <= _T('9'))
								|| text[pos] == _T('.') || text[pos] == _T('-')
								|| text[pos] == _T('_'); pos++) 
							{}

							found_real_char = FALSE;
						}

						// It has real chars?
						if (found_real_char)
						{
							// Is under cursor?
							if (check_word_under_cursor || !(first_char+last_pos <= __old_sel.cpMax && first_char+pos >= __old_sel.cpMin))
							{
								DealWord(dlg, text, first_char, last_pos, pos, __old_sel, diff, callback, param);
							}
						}

						last_pos = -1;
						found_real_char = FALSE;
					}
				}
				else 
				{
					if (last_pos == -1)
						last_pos = pos;

					if (text[pos] != _T('-') && !(text[pos] >= _T('0') && text[pos] <= _T('9')))
						found_real_char = TRUE;
				}
			}

			// Last word
			if (last_pos != -1)
			{
				// It has real chars?
				if (found_real_char)
				{
					// Is under cursor?
					if (check_word_under_cursor || !(first_char+last_pos <= __old_sel.cpMax && first_char+pos >= __old_sel.cpMin))
					{
						DealWord(dlg, text, first_char, last_pos, pos, __old_sel, diff, callback, param);
					}
				}
			}
		}
	}

	// Fix last char
	int len = GetWindowTextLength(dlg->hwnd);
	SetNoUnderline(dlg->hwnd, len, len);

	START_RICHEDIT(dlg);
}


void ToLocaleID(TCHAR *szKLName, size_t size)
{
	TCHAR *stopped = NULL;
	USHORT langID = (USHORT) _tcstol(szKLName, &stopped, 16);

	TCHAR ini[32];
	TCHAR end[32];
	GetLocaleInfo(MAKELCID(langID, 0), LOCALE_SISO639LANGNAME, ini, MAX_REGS(ini));
	GetLocaleInfo(MAKELCID(langID, 0), LOCALE_SISO3166CTRYNAME, end, MAX_REGS(end));

	mir_sntprintf(szKLName, size, _T("%s_%s"), ini, end);
}


void LoadDictFromKbdl(Dialog *dlg)
{
	if (opts.auto_locale) {
		TCHAR szKLName[KL_NAMELENGTH + 1];
		GetKeyboardLayoutName(szKLName);
		ToLocaleID(szKLName, MAX_REGS(szKLName));

		int d = GetClosestLanguage(szKLName);
		if (d >= 0)
		{
			dlg->lang = languages.dicts[d];
			dlg->lang->load();

			if (dlg->srmm)
				ModifyIcon(dlg);
		}

//				GetLocaleID(dat, szKLName);
	}
}


LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	DialogMapType::iterator dlgit = dialogs.find(hwnd);
	if (dlgit == dialogs.end())
		return -1;

	Dialog *dlg = dlgit->second;

	LRESULT ret = CallWindowProc(dlg->old_edit_proc, hwnd, msg, wParam, lParam);

	switch(msg)
	{
		case WM_KEYDOWN:
		{
			if (wParam != VK_DELETE
				&& wParam != VK_UP
				&& wParam != VK_DOWN
				&& wParam != VK_HOME
				&& wParam != VK_END)
				break;
		}
		case WM_CHAR:
		{
			if (lParam & (1 << 28))	// ALT key
				break;

			if (GetKeyState(VK_CONTROL) & 0x8000)	// CTRL key
				break;

			// Need to do that to avoid changing the word while typing
			KillTimer(hwnd, TIMER_ID);
			SetTimer(hwnd, TIMER_ID, 1000, NULL);

			dlg->changed = TRUE;

			if ((lParam & 0xFF) > 1)	// Repeat rate
				break;

			if (!dlg->enabled || dlg->lang == NULL || !dlg->lang->isLoaded())
				break;

			// Don't check if field is read-only
			if (GetWindowLong(hwnd, GWL_STYLE) & ES_READONLY)
				break;

			TCHAR c = (TCHAR) wParam;
			if (!dlg->lang->isWordChar(c))
			{
				CheckText(dlg, FALSE, FALSE);
			}
			else
			{
				// Assert no selection
				CHARFORMAT2 cf;
				cf.cbSize = sizeof(CHARFORMAT2);
				SendMessage(dlg->hwnd, EM_GETCHARFORMAT, TRUE, (LPARAM)&cf);

				if ((cf.dwMask & CFM_UNDERLINETYPE) 
					&& (cf.dwEffects & 0x0F) >= CFU_UNDERLINEDOUBLE
					&& (cf.dwEffects & 0x0F) <= CFU_UNDERLINETHICK)
				{
					STOP_RICHEDIT(dlg);

					// Remove underline of current word
					TCHAR text[1024];
					int first_char;
					CHARRANGE sel;

					SendMessage(dlg->hwnd, EM_EXGETSEL, 0, (LPARAM) &sel);

					GetWordCharRange(dlg, sel, text, MAX_REGS(text), first_char);

					SetNoUnderline(dlg->hwnd, sel.cpMin, sel.cpMax);

					START_RICHEDIT(dlg);
				}
			}

			break;
		}
		case EM_PASTESPECIAL:
		case WM_PASTE:
		{
			// Need to do that to avoid changing the word while typing
			KillTimer(hwnd, TIMER_ID);
			SetTimer(hwnd, TIMER_ID, 1000, NULL);

			dlg->changed = TRUE;

			if (!dlg->enabled || dlg->lang == NULL || !dlg->lang->isLoaded())
				break;

			// Don't check if field is read-only
			if (GetWindowLong(hwnd, GWL_STYLE) & ES_READONLY)
				break;

			// Parse all text
			CheckText(dlg, TRUE, FALSE);
			break;
		}

		case WM_TIMER:
		{
			if (wParam != TIMER_ID)
				break;

			if (!dlg->enabled || dlg->lang == NULL || !dlg->lang->isLoaded())
				break;

			// Don't check if field is read-only
			if (GetWindowLong(hwnd, GWL_STYLE) & ES_READONLY)
				break;

			int len = GetWindowTextLength(hwnd);
			if (len == dlg->old_text_len && !dlg->changed)
				break;

			dlg->old_text_len = len;
			dlg->changed = FALSE;

			CheckText(dlg, TRUE);
			break;
		}

		case WMU_DICT_CHANGED:
		{
			KillTimer(hwnd, TIMER_ID);
			SetTimer(hwnd, TIMER_ID, 10, NULL);

			dlg->changed = TRUE;
			break;
		}

		case WMU_KBDL_CHANGED:
		{
			LoadDictFromKbdl(dlg);

			break;
		}

		case WM_INPUTLANGCHANGE:
		{
			// Allow others to process this message and we get only the result
			PostMessage(hwnd, WMU_KBDL_CHANGED, 0, 0);
			break;
		}
	}

	return ret;
}

int GetClosestLanguage(TCHAR *lang_name) 
{
	// Search the language by name
	for(unsigned i = 0; i < languages.count; i++)
	{
		if (lstrcmpi(languages.dicts[i]->language, lang_name) == 0)
		{
			return i;
		}
	}

	// Try searching by the prefix only
	TCHAR *p = _tcschr(lang_name, _T('_'));
	if (p != NULL)
	{
		*p = _T('\0');
		for(unsigned i = 0; i < languages.count; i++)
		{
			if (lstrcmpi(languages.dicts[i]->language, lang_name) == 0)
			{
				*p = '_';
				return i;
			}
		}
		*p = _T('_');
	}

	// Try any suffix, if one not provided
	if (p == NULL)
	{
		size_t len = lstrlen(lang_name);
		for(unsigned i = 0; i < languages.count; i++)
		{
			if (_tcsnicmp(languages.dicts[i]->language, lang_name, len) == 0 
				&& languages.dicts[i]->language[len] == _T('_'))
			{
				return i;
			}
		}
	}

	return -1;
}

void GetUserProtoLanguageSetting(Dialog *dlg, HANDLE hContact, char *proto, char *setting)
{
	DBVARIANT dbv = {0};

	if (!DBGetContactSettingTString(hContact, proto, setting, &dbv))
	{
		for(unsigned i = 0; i < languages.count; i++)
		{
			if (lstrcmpi(languages.dicts[i]->localized_name, dbv.ptszVal) == 0)
			{
				lstrcpyn(dlg->lang_name, languages.dicts[i]->language, MAX_REGS(dlg->lang_name));
				break;
			}
			if (lstrcmpi(languages.dicts[i]->english_name, dbv.ptszVal) == 0)
			{
				lstrcpyn(dlg->lang_name, languages.dicts[i]->language, MAX_REGS(dlg->lang_name));
				break;
			}
		}
		DBFreeVariant(&dbv);
	}
}

void GetUserLanguageSetting(Dialog *dlg, char *setting)
{
	DBVARIANT dbv = {0};

	char *proto = (char *) CallService(MS_PROTO_GETCONTACTBASEPROTO, (WPARAM) dlg->hContact, 0);
	if (proto == NULL)
		return;

	GetUserProtoLanguageSetting(dlg, dlg->hContact, proto, setting);

	// If not found and is inside meta, try to get from the meta
	if (dlg->lang_name[0] != _T('\0'))
		return;
	
	// Is a subcontact?
	if (!ServiceExists(MS_MC_GETMETACONTACT)) 
		return;

	HANDLE hMetaContact = (HANDLE) CallService(MS_MC_GETMETACONTACT, (WPARAM) dlg->hContact, 0);
	if (hMetaContact == NULL)
		return;

	GetUserProtoLanguageSetting(dlg, hMetaContact, metacontacts_proto, setting);
}

void GetContactLanguage(Dialog *dlg)
{
	DBVARIANT dbv = {0};

	dlg->lang_name[0] = _T('\0');

	if (dlg->hContact == NULL) 
	{
		if (!DBGetContactSettingTString(NULL, MODULE_NAME, dlg->name, &dbv))
		{
			lstrcpyn(dlg->lang_name, dbv.ptszVal, MAX_REGS(dlg->lang_name));
			DBFreeVariant(&dbv);
		}
	}
	else
	{
		if (!DBGetContactSettingTString(dlg->hContact, MODULE_NAME, "TalkLanguage", &dbv))
		{
			lstrcpyn(dlg->lang_name, dbv.ptszVal, MAX_REGS(dlg->lang_name));
			DBFreeVariant(&dbv);
		}

		// Try from metacontact
		if (dlg->lang_name[0] == _T('\0') && ServiceExists(MS_MC_GETMETACONTACT)) 
		{
			HANDLE hMetaContact = (HANDLE) CallService(MS_MC_GETMETACONTACT, (WPARAM) dlg->hContact, 0);
			if (hMetaContact != NULL)
			{
				if (!DBGetContactSettingTString(hMetaContact, MODULE_NAME, "TalkLanguage", &dbv))
				{
					lstrcpyn(dlg->lang_name, dbv.ptszVal, MAX_REGS(dlg->lang_name));
					DBFreeVariant(&dbv);
				}
			}
		}

		// Try to get from Language info
		if (dlg->lang_name[0] == _T('\0'))
			GetUserLanguageSetting(dlg, "Language");
		if (dlg->lang_name[0] == _T('\0'))
			GetUserLanguageSetting(dlg, "Language1");
		if (dlg->lang_name[0] == _T('\0'))
			GetUserLanguageSetting(dlg, "Language2");
		if (dlg->lang_name[0] == _T('\0'))
			GetUserLanguageSetting(dlg, "Language3");

		// Use default lang
		if (dlg->lang_name[0] == _T('\0'))
			lstrcpyn(dlg->lang_name, opts.default_language, MAX_REGS(dlg->lang_name));
	}

	int i = GetClosestLanguage(dlg->lang_name);
	if(i < 0)
	{
		// Lost a dict?
		lstrcpyn(dlg->lang_name, opts.default_language, MAX_REGS(dlg->lang_name));
		i = GetClosestLanguage(dlg->lang_name);
	}

	if (i >= 0)
	{
		dlg->lang = languages.dicts[i];
		dlg->lang->load();
	}
	else 
	{
		dlg->lang = NULL;
	}
}

void ModifyIcon(Dialog *dlg)
{
	if (ServiceExists(MS_MSG_MODIFYICON))
	{
		StatusIconData sid = {0};
		sid.cbSize = sizeof(sid);
		sid.szModule = MODULE_NAME;

		for(unsigned i = 0; i < languages.count; i++)
		{
			sid.dwId = i;
			
			if (languages.dicts[i] == dlg->lang)
				sid.flags = (dlg->enabled ? 0 : MBF_DISABLED);
			else
				sid.flags = MBF_HIDDEN;
			
			CallService(MS_MSG_MODIFYICON, (WPARAM) dlg->hContact, (LPARAM) &sid);
		}
	}
}

int AddContactTextBoxService(WPARAM wParam, LPARAM lParam)
{
	SPELLCHECKER_ITEM *sci = (SPELLCHECKER_ITEM *) wParam;
	if (sci == NULL || sci->cbSize != sizeof(SPELLCHECKER_ITEM))
		return -1;

	return AddContactTextBox(sci->hContact, sci->hwnd, sci->window_name, FALSE, NULL);
}

int AddContactTextBox(HANDLE hContact, HWND hwnd, char *name, BOOL srmm, HWND hwndOwner) 
{
	if (languages.count <= 0)
		return 0;

	if (dialogs.find(hwnd) == dialogs.end())
	{
		// Fill dialog data
		Dialog *dlg = (Dialog *) malloc(sizeof(Dialog));
		ZeroMemory(dlg, sizeof(Dialog));

		dlg->hContact = hContact;
		dlg->hwnd = hwnd;
		strncpy(dlg->name, name, sizeof(dlg->name));
		dlg->enabled = DBGetContactSettingByte(dlg->hContact, MODULE_NAME, dlg->name, 1);
		dlg->srmm = srmm;
		dlg->hwnd_owner = hwndOwner;

		SendMessage(hwnd, EM_GETOLEINTERFACE, 0, (LPARAM)&dlg->ole);
		if (dlg->ole == NULL)
		{
			free(dlg);
			return 0;
		}

		if (dlg->ole->QueryInterface(IID_ITextDocument, (void**)&dlg->textDocument) != S_OK)
			dlg->textDocument = NULL;

		GetContactLanguage(dlg);
		LoadDictFromKbdl(dlg);

		dlg->old_edit_proc = (WNDPROC) SetWindowLong(hwnd, GWL_WNDPROC, (LONG) EditProc);

		dialogs[hwnd] = dlg;

		SetTimer(hwnd, TIMER_ID, 500, NULL);

		if (dlg->srmm)
			ModifyIcon(dlg);
	}

	return 0;
}

#define DESTROY_MENY(_m_)	if (_m_ != NULL) { DestroyMenu(_m_); _m_ = NULL; }
#define FREE(_m_)			if (_m_ != NULL) { free(_m_); _m_ = NULL; }

void FreePopupData(Dialog *dlg)
{
	DESTROY_MENY(dlg->hLanguageSubMenu)
	DESTROY_MENY(dlg->hWrongWordsSubMenu)

	if (dlg->old_menu_proc != NULL)
		SetWindowLong(dlg->hwnd_menu_owner, GWL_WNDPROC, (LONG) dlg->old_menu_proc);
	dlg->old_menu_proc = NULL;

	if (dlg->hwnd_menu_owner != NULL)
		menus.erase(dlg->hwnd_menu_owner);
	dlg->hwnd_menu_owner = NULL;

	if (dlg->wrong_words != NULL)
	{
		for (unsigned i = 0; i < dlg->wrong_words->size(); i++)
		{
			FREE((*dlg->wrong_words)[i].word)

			DESTROY_MENY((*dlg->wrong_words)[i].hMeSubMenu)
			DESTROY_MENY((*dlg->wrong_words)[i].hCorrectSubMenu)
			DESTROY_MENY((*dlg->wrong_words)[i].hReplaceSubMenu)

			FreeSuggestions((*dlg->wrong_words)[i].suggestions);
		}

		delete dlg->wrong_words;
		dlg->wrong_words = NULL;
	}
}


int RemoveContactTextBoxService(WPARAM wParam, LPARAM lParam)
{
	HWND hwnd = (HWND) wParam;
	if (hwnd == NULL)
		return -1;

	return RemoveContactTextBox(hwnd);
}


int RemoveContactTextBox(HWND hwnd) 
{
	DialogMapType::iterator dlgit = dialogs.find(hwnd);
	if (dlgit != dialogs.end())
	{
		Dialog *dlg = dlgit->second;
		
		KillTimer(hwnd, TIMER_ID);

		SetWindowLong(hwnd, GWL_WNDPROC, (LONG) dlg->old_edit_proc);

		dialogs.erase(hwnd);

		if (dlg->textDocument != NULL)
			dlg->textDocument->Release();
		dlg->ole->Release();

		FreePopupData(dlg);
		free(dlg);
	}

	return 0;
}


void ReplaceWord(Dialog *dlg, CHARRANGE &sel, TCHAR *new_word)
{
	STOP_RICHEDIT(dlg);

	// Replace in rich edit
	SendMessage(dlg->hwnd, EM_EXSETSEL, 0, (LPARAM) &sel);

	RESUME_UNDO(dlg);

	SendMessage(dlg->hwnd, EM_REPLACESEL, TRUE, (LPARAM) new_word);

	SUSPEND_UNDO(dlg);

	// Fix old sel
	int dif = lstrlen(new_word) - sel.cpMax + sel.cpMin;
	if (__old_sel.cpMin >= sel.cpMax)
		__old_sel.cpMin += dif;
	if (__old_sel.cpMax >= sel.cpMax)
		__old_sel.cpMax += dif;

	START_RICHEDIT(dlg);
}


BOOL GetWordCharRange(Dialog *dlg, CHARRANGE &sel, TCHAR *text, size_t text_len, int &first_char)
{
	// Get line
	int line = SendMessage(dlg->hwnd, EM_LINEFROMCHAR, (WPARAM) sel.cpMin, 0);

	// Get text
	GetLineOfText(dlg, line, first_char, text, text_len);

	// Find the word
	sel.cpMin--;
	while (sel.cpMin >= first_char && (dlg->lang->isWordChar(text[sel.cpMin - first_char]) 
										|| (text[sel.cpMin - first_char] >= _T('0') && text[sel.cpMin - first_char] <= _T('9'))))
		sel.cpMin--;
	sel.cpMin++;

	while (text[sel.cpMax - first_char] != _T('\0') && (dlg->lang->isWordChar(text[sel.cpMax - first_char])
														|| (text[sel.cpMax - first_char] >= _T('0') && text[sel.cpMax - first_char] <= _T('9'))))
		sel.cpMax++;

	// Has a word?
	if (sel.cpMin >= sel.cpMax)
		return FALSE;

	// See if it has only '-'s
	BOOL has_valid_char = FALSE;
	for (int i = sel.cpMin; i < sel.cpMax && !has_valid_char; i++)
		has_valid_char = ( text[i - first_char] != _T('-') );

	if (!has_valid_char)
		return FALSE;

	return TRUE;
}

TCHAR *GetWordUnderPoint(Dialog *dlg, POINT pt, CHARRANGE &sel)
{
	// Get text
	if (GetWindowTextLength(dlg->hwnd) <= 0)
		return NULL;

	// Get pos
	sel.cpMin = sel.cpMax = LOWORD(SendMessage(dlg->hwnd, EM_CHARFROMPOS, 0, (LPARAM) &pt));

	// Get text
	TCHAR text[1024];
	int first_char;

	if (!GetWordCharRange(dlg, sel, text, MAX_REGS(text), first_char))
		return NULL;

	// copy the word
	text[sel.cpMax - first_char] = _T('\0');
	return _tcsdup(&text[sel.cpMin - first_char]);
}


void AppendSubmenu(HMENU hMenu, HMENU hSubMenu, TCHAR *name) 
{
	MENUITEMINFO mii = {0};
	mii.cbSize = sizeof(MENUITEMINFO);
	mii.fMask = MIIM_SUBMENU | MIIM_TYPE;
	mii.fType = MFT_STRING;
	mii.hSubMenu = hSubMenu;
	mii.dwTypeData = name;
	mii.cch = lstrlen(name);
	int ret = InsertMenuItem(hMenu, 0, TRUE, &mii);

}

void AppendMenuItem(HMENU hMenu, int id, TCHAR *name, HICON hIcon, BOOL checked) 
{
	ICONINFO iconInfo;
	GetIconInfo(hIcon, & iconInfo);

	MENUITEMINFO mii = {0};
	mii.cbSize = sizeof(MENUITEMINFO);
	mii.fMask = MIIM_CHECKMARKS | MIIM_TYPE | MIIM_STATE;
	mii.fType = MFT_STRING;
	mii.fState = (checked ? MFS_CHECKED : 0);
	mii.wID = id;
	mii.hbmpChecked = iconInfo.hbmColor;
	mii.hbmpUnchecked = iconInfo.hbmColor;
	mii.dwTypeData = name;
	mii.cch = lstrlen(name);
	int ret = InsertMenuItem(hMenu, 0, TRUE, &mii);
}




#define LANGUAGE_MENU_ID_BASE 10
#define WORD_MENU_ID_BASE 100
#define AUTOREPLACE_MENU_ID_BASE 50

void AddMenuForWord(Dialog *dlg, TCHAR *word, CHARRANGE &pos, HMENU hMenu, BOOL in_submenu, UINT base)
{
	if (dlg->wrong_words == NULL)
		dlg->wrong_words = new vector<WrongWordPopupMenuData>(1);
	else
		dlg->wrong_words->resize(dlg->wrong_words->size() + 1);

	WrongWordPopupMenuData &data = (*dlg->wrong_words)[dlg->wrong_words->size() - 1];
	ZeroMemory(&data, sizeof(WrongWordPopupMenuData));

	// Get suggestions
	data.word = word;
	data.pos = pos;
	data.suggestions = dlg->lang->suggest(word);

	Suggestions &suggestions = data.suggestions;

	if (in_submenu)
	{
		data.hMeSubMenu = CreatePopupMenu();
		AppendSubmenu(hMenu, data.hMeSubMenu, word);
		hMenu = data.hMeSubMenu;
	}

	data.hReplaceSubMenu = CreatePopupMenu();

	InsertMenu(data.hReplaceSubMenu, 0, MF_BYPOSITION, 
			base + AUTOREPLACE_MENU_ID_BASE + suggestions.count, TranslateT("Other..."));
	for (int i = suggestions.count - 1; i >= 0; i--) 
		InsertMenu(data.hReplaceSubMenu, 0, MF_BYPOSITION, 
				base + AUTOREPLACE_MENU_ID_BASE + i, suggestions.words[i]);

	AppendSubmenu(hMenu, data.hReplaceSubMenu, TranslateT("Always replace with"));

	InsertMenu(hMenu, 0, MF_BYPOSITION, base + suggestions.count + 1, TranslateT("Ignore all"));
	InsertMenu(hMenu, 0, MF_BYPOSITION, base + suggestions.count, TranslateT("Add to dictionary"));

	if (suggestions.count > 0)
	{
		HMENU hSubMenu;
		if (opts.cascade_corrections)
		{
			hSubMenu = data.hCorrectSubMenu = CreatePopupMenu();
			AppendSubmenu(hMenu, hSubMenu, TranslateT("Corrections"));
		}
		else
		{
			InsertMenu(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, 0);
			hSubMenu = hMenu;
		}

		for (int i = suggestions.count - 1; i >= 0; i--) 
			InsertMenu(hSubMenu, 0, MF_BYPOSITION, base + i, suggestions.words[i]);
	}

	if (!in_submenu && opts.show_wrong_word)
	{
		InsertMenu(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, 0);

		TCHAR text[128];
		mir_sntprintf(text, MAX_REGS(text), TranslateT("Wrong word: %s"), word);
		InsertMenu(hMenu, 0, MF_BYPOSITION, 0, text);
	}
}


struct FoundWrongWordParam {
	Dialog *dlg;
	int count;
};

void FoundWrongWord(TCHAR *word, CHARRANGE pos, void *param)
{
	FoundWrongWordParam *p = (FoundWrongWordParam*) param;

	p->count ++;

	AddMenuForWord(p->dlg, _tcsdup(word), pos, p->dlg->hWrongWordsSubMenu, TRUE, WORD_MENU_ID_BASE * p->count);
}

void AddItemsToMenu(Dialog *dlg, HMENU hMenu, POINT pt, HWND hwndOwner)
{
	FreePopupData(dlg);
	if (opts.use_flags)
	{
		dlg->hwnd_menu_owner = hwndOwner;
		menus[hwndOwner] = dlg;
	}

	BOOL wrong_word = FALSE;

	// Make menu
	if (GetMenuItemCount(hMenu) > 0)
		InsertMenu(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, 0);

	if (languages.count > 0 && dlg->enabled)
	{
		dlg->hLanguageSubMenu = CreatePopupMenu();

		if (dlg->hwnd_menu_owner != NULL)
			dlg->old_menu_proc = (WNDPROC) SetWindowLong(dlg->hwnd_menu_owner, GWL_WNDPROC, (LONG) MenuWndProc);

		// First add languages
		for(unsigned i = 0; i < languages.count; i++)
		{
			AppendMenu(dlg->hLanguageSubMenu, MF_STRING | (languages.dicts[i] == dlg->lang ? MF_CHECKED : 0),
				LANGUAGE_MENU_ID_BASE + i, languages.dicts[i]->full_name);
		}

		AppendSubmenu(hMenu, dlg->hLanguageSubMenu, TranslateT("Language"));
	}

	InsertMenu(hMenu, 0, MF_BYPOSITION, 1, TranslateT("Enable spell checking"));
	CheckMenuItem(hMenu, 1, MF_BYCOMMAND | (dlg->enabled ? MF_CHECKED : MF_UNCHECKED));

	// Get text
	if (dlg->lang != NULL && dlg->enabled)
	{
		if (opts.show_all_corrections)
		{
			dlg->hWrongWordsSubMenu = CreatePopupMenu(); 

			FoundWrongWordParam p = { dlg, 0 };
			CheckText(dlg, TRUE, FoundWrongWord, &p);

			if (p.count > 0)
				AppendSubmenu(hMenu, dlg->hWrongWordsSubMenu, TranslateT("Wrong words"));
		}
		else
		{
			CHARRANGE sel;
			TCHAR *word = GetWordUnderPoint(dlg, pt, sel);
			if (word != NULL && !dlg->lang->spell(word))
			{
				InsertMenu(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, 0);

				AddMenuForWord(dlg, word, sel, hMenu, FALSE, WORD_MENU_ID_BASE);
			}
		}
	}
}


BOOL HandleMenuSelection(Dialog *dlg, POINT pt, unsigned selection)
{
	BOOL ret = FALSE;

	if (selection == 1)
	{
		dlg->enabled = !dlg->enabled;
		DBWriteContactSettingByte(dlg->hContact, MODULE_NAME, dlg->name, dlg->enabled);

		if (!dlg->enabled)
		{
			STOP_RICHEDIT(dlg);

			SetNoUnderline(dlg->hwnd);
			
			START_RICHEDIT(dlg);
		}

		if (dlg->srmm)
			ModifyIcon(dlg);

		ret = TRUE;
	}
	else if (selection >= LANGUAGE_MENU_ID_BASE && selection < LANGUAGE_MENU_ID_BASE + languages.count)
	{
		STOP_RICHEDIT(dlg);
		SetNoUnderline(dlg->hwnd);
		START_RICHEDIT(dlg);

		if (dlg->hContact == NULL)
			DBWriteContactSettingTString(NULL, MODULE_NAME, dlg->name, 
					languages.dicts[selection - LANGUAGE_MENU_ID_BASE]->language);
		else
			DBWriteContactSettingTString(dlg->hContact, MODULE_NAME, "TalkLanguage", 
					languages.dicts[selection - LANGUAGE_MENU_ID_BASE]->language);
		GetContactLanguage(dlg);

		if (dlg->srmm)
			ModifyIcon(dlg);

		ret = TRUE;
	}
	else if (selection > 0 && dlg->wrong_words != NULL 
			 && selection >= WORD_MENU_ID_BASE
			 && selection < (dlg->wrong_words->size() + 1) * WORD_MENU_ID_BASE)
	{
		int pos = selection / WORD_MENU_ID_BASE;
		selection -=  pos * WORD_MENU_ID_BASE;
		pos--; // 0 based
		WrongWordPopupMenuData &data = (*dlg->wrong_words)[pos];

		if (selection < data.suggestions.count)
		{
			// TODO Assert that text hasn't changed
			ReplaceWord(dlg, data.pos, data.suggestions.words[selection]);

			ret = TRUE;
		}
		else if (selection == data.suggestions.count)
		{
			dlg->lang->addWord(data.word);

			ret = TRUE;
		}
		else if (selection == data.suggestions.count + 1)
		{
			dlg->lang->ignoreWord(data.word);

			ret = TRUE;
		}
		else if (selection >= AUTOREPLACE_MENU_ID_BASE 
				 && selection < AUTOREPLACE_MENU_ID_BASE + data.suggestions.count + 1)
		{
			selection -= AUTOREPLACE_MENU_ID_BASE;

			if (selection == data.suggestions.count)
			{
				AddReplaceDialog(dlg->lang, data.word, dlg->hwnd);
			}
			else
			{
				// TODO Assert that text hasn't changed
				ReplaceWord(dlg, data.pos, data.suggestions.words[selection]);
				dlg->lang->addToAutoReplace(data.word, data.suggestions.words[selection]);
				ret = TRUE;
			}
		}
	}

	if (ret)
		dlg->changed = TRUE;

	FreePopupData(dlg);

	return ret;
}


int MsgWindowPopup(WPARAM wParam, LPARAM lParam)
{
	MessageWindowPopupData *mwpd = (MessageWindowPopupData *) lParam;
	if (mwpd == NULL || mwpd->cbSize < sizeof(MessageWindowPopupData)
			|| mwpd->uFlags != MSG_WINDOWPOPUP_INPUT)
		return 0;

	DialogMapType::iterator dlgit = dialogs.find(mwpd->hwnd);
	if (dlgit == dialogs.end())
		return -1;

	Dialog *dlg = dlgit->second;

	POINT pt = mwpd->pt;
	ScreenToClient(dlg->hwnd, &pt);

	if (mwpd->uType == MSG_WINDOWPOPUP_SHOWING)
	{
		AddItemsToMenu(dlg, mwpd->hMenu, pt, dlg->hwnd_owner);
	}
	else if (mwpd->uType == MSG_WINDOWPOPUP_SELECTED)
	{
		HandleMenuSelection(dlg, pt, mwpd->selection);
	}
	return 0;
}


int ShowPopupMenuService(WPARAM wParam, LPARAM lParam)
{
	SPELLCHECKER_POPUPMENU *scp = (SPELLCHECKER_POPUPMENU *) wParam;
	if (scp == NULL || scp->cbSize != sizeof(SPELLCHECKER_POPUPMENU))
		return -1;

	return ShowPopupMenu(scp->hwnd, scp->hMenu, scp->pt, scp->hwndOwner == NULL ? scp->hwnd : scp->hwndOwner);
}


int ShowPopupMenu(HWND hwnd, HMENU hMenu, POINT pt, HWND hwndOwner)
{
	DialogMapType::iterator dlgit = dialogs.find(hwnd);
	if (dlgit == dialogs.end())
		return -1;

	Dialog *dlg = dlgit->second;

	if (pt.x == 0xFFFF && pt.y == 0xFFFF)
	{
		CHARRANGE sel;
		SendMessage(hwnd, EM_EXGETSEL, 0, (LPARAM) &sel);

		// Get current cursor pos
		SendMessage(hwnd, EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM) sel.cpMax);
	}
	else
	{
		ScreenToClient(hwnd, &pt);
	}

	BOOL create_menu = (hMenu == NULL);
	if (create_menu)
		hMenu = CreatePopupMenu();

	// Make menu
	AddItemsToMenu(dlg, hMenu, pt, hwndOwner);

	// Show menu
	POINT client = pt;
	ClientToScreen(hwnd, &pt);
	int selection = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwndOwner, NULL);

	// Do action
	if (HandleMenuSelection(dlg, client, selection))
		selection = 0;

	if (create_menu)
		DestroyMenu(hMenu);

	return selection;
}


int MsgWindowEvent(WPARAM wParam, LPARAM lParam)
{
	MessageWindowEventData *event = (MessageWindowEventData *)lParam;
	if (event == NULL)
		return 0;

	if (event->cbSize < sizeof(MessageWindowEventData))
		return 0;

	if (event->uType == MSG_WINDOW_EVT_OPEN)
	{
		AddContactTextBox(event->hContact, event->hwndInput, "DefaultSRMM", TRUE, event->hwndWindow);
	}
	else if (event->uType == MSG_WINDOW_EVT_CLOSING)
	{
		RemoveContactTextBox(event->hwndInput);
	}

	return 0;
}


int IconPressed(WPARAM wParam, LPARAM lParam)
{
	StatusIconClickData *sicd = (StatusIconClickData *) lParam;
	if (sicd == NULL || strcmp(sicd->szModule, MODULE_NAME) != 0)
		return 0;

	HANDLE hContact = (HANDLE) wParam;
	if (hContact == NULL)
		return 0;

	// Find the dialog
	HWND hwnd = NULL;
	Dialog *dlg;
	for(DialogMapType::iterator it = dialogs.begin(); it != dialogs.end(); it++)
	{
		dlg = it->second;
		if (dlg->srmm && dlg->hContact == hContact)
		{
			hwnd = it->first;
			break;
		}
	}

	if (hwnd == NULL) 
	{
		MessageBox(NULL, 
			TranslateT("Could not find the message dialog. This usually means one of two things:\n- In tabSRMM the checkbox 'Enable event API' is disabled or\n- You are using SRMM (which don't support Spell Checker)"),
			TranslateT("Spell Checker"), MB_ICONERROR | MB_OK);
		return 0;
	}

	if (sicd->flags & MBCF_RIGHTBUTTON)
	{
		FreePopupData(dlg);

		// Show the menu
		HMENU hMenu = CreatePopupMenu();

		if (languages.count > 0)
		{
			if (opts.use_flags)
			{
				menus[dlg->hwnd] = dlg;
				dlg->hwnd_menu_owner = dlg->hwnd;
				dlg->old_menu_proc = (WNDPROC) SetWindowLong(dlg->hwnd_menu_owner, GWL_WNDPROC, (LONG) MenuWndProc);
			}

			// First add languages
			for(unsigned i = 0; i < languages.count; i++)
			{
				AppendMenu(hMenu, MF_STRING | (languages.dicts[i] == dlg->lang ? MF_CHECKED : 0),
					LANGUAGE_MENU_ID_BASE + i, languages.dicts[i]->full_name);
			}

			InsertMenu(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, 0);
		}

		InsertMenu(hMenu, 0, MF_BYPOSITION, 1, TranslateT("Enable spell checking"));
		CheckMenuItem(hMenu, 1, MF_BYCOMMAND | (dlg->enabled ? MF_CHECKED : MF_UNCHECKED));

		// Show menu
		int selection = TrackPopupMenu(hMenu, TPM_RETURNCMD, sicd->clickLocation.x, sicd->clickLocation.y, 0, 
									   dlg->hwnd, NULL);

		HandleMenuSelection(dlg, sicd->clickLocation, selection);

		DestroyMenu(hMenu);
	}
	else
	{
		// Enable / disable
		HandleMenuSelection(dlg, sicd->clickLocation, 1);
	}

	return 0;
}


LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	DialogMapType::iterator dlgit = menus.find(hwnd);
	if (dlgit == menus.end())
		return -1;

	Dialog *dlg = dlgit->second;

	switch (msg) 
	{
		case WM_INITMENUPOPUP:
		{
			HMENU hMenu = (HMENU) wParam;

			int count = GetMenuItemCount(hMenu);
			for(int i = 0; i < count; i++)
			{
				unsigned id = GetMenuItemID(hMenu, i);
				if (id < LANGUAGE_MENU_ID_BASE || id >= LANGUAGE_MENU_ID_BASE + languages.count) 
					continue;

				MENUITEMINFO mii = {0};
				mii.cbSize = sizeof(MENUITEMINFO);
				mii.fMask = MIIM_STATE;
				GetMenuItemInfo(hMenu, id, FALSE, &mii);

				// Make ownerdraw
				ModifyMenu(hMenu, id, mii.fState | MF_BYCOMMAND | MF_OWNERDRAW, id, NULL);
			}

			break;
		}
		case WM_DRAWITEM:
		{
			LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
			if(lpdis->itemID < LANGUAGE_MENU_ID_BASE || lpdis->itemID >= LANGUAGE_MENU_ID_BASE + languages.count) 
				break;

			int pos = lpdis->itemID - LANGUAGE_MENU_ID_BASE;

			Dictionary *dict = languages.dicts[pos];

			COLORREF clrfore = SetTextColor(lpdis->hDC, 
					GetSysColor(lpdis->itemState & ODS_SELECTED ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
			COLORREF clrback = SetBkColor(lpdis->hDC, 
					GetSysColor(lpdis->itemState & ODS_SELECTED ? COLOR_HIGHLIGHT : COLOR_MENU));

			FillRect(lpdis->hDC, &lpdis->rcItem, GetSysColorBrush(lpdis->itemState & ODS_SELECTED ? COLOR_HIGHLIGHT : COLOR_MENU));

			RECT rc = lpdis->rcItem;
			rc.left += 2;

			// Checked?
			rc.right = rc.left + bmpChecked.bmWidth;

			if (lpdis->itemState & ODS_CHECKED)
			{
				rc.top = (lpdis->rcItem.bottom + lpdis->rcItem.top - bmpChecked.bmHeight) / 2;
				rc.bottom = rc.top + bmpChecked.bmHeight;

				HDC hdcTemp = CreateCompatibleDC(lpdis->hDC);
				HBITMAP oldBmp = (HBITMAP) SelectObject(hdcTemp, hCheckedBmp);

				BitBlt(lpdis->hDC, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hdcTemp, 0, 0, SRCCOPY);

				SelectObject(hdcTemp, oldBmp);
				DeleteDC(hdcTemp);
			}

			rc.left += bmpChecked.bmWidth + 2;

			// Draw icon
			HICON hFlag = LoadIconEx(dict);

			rc.top = (lpdis->rcItem.bottom + lpdis->rcItem.top - ICON_SIZE) / 2;
			DrawIconEx(lpdis->hDC, rc.left, rc.top, hFlag, 16, 16, 0, NULL, DI_NORMAL);

			ReleaseIconEx(hFlag);

			rc.left += ICON_SIZE + 4;

			// Draw text
			RECT rc_text = { 0, 0, 0xFFFF, 0xFFFF };
			DrawText(lpdis->hDC, dict->full_name, lstrlen(dict->full_name), &rc_text, DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE | DT_CALCRECT);

			rc.right = lpdis->rcItem.right - 2;
			rc.top = (lpdis->rcItem.bottom + lpdis->rcItem.top - (rc_text.bottom - rc_text.top)) / 2;
			rc.bottom = rc.top + rc_text.bottom - rc_text.top;
			DrawText(lpdis->hDC, dict->full_name, lstrlen(dict->full_name), &rc, DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE);

			// Restore old colors
			SetTextColor(lpdis->hDC, clrfore);
			SetBkColor(lpdis->hDC, clrback);

			return TRUE;
		}

		case WM_MEASUREITEM:
		{
			LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lParam;
			if(lpmis->itemID < LANGUAGE_MENU_ID_BASE || lpmis->itemID >= LANGUAGE_MENU_ID_BASE + languages.count) 
				break;

			int pos = lpmis->itemID - LANGUAGE_MENU_ID_BASE;

			Dictionary *dict = languages.dicts[pos];

			HDC hdc = GetDC(hwnd);

			NONCLIENTMETRICS info;
			info.cbSize = sizeof(info);
			SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(info), &info, 0);
			HFONT hFont = CreateFontIndirect(&info.lfMenuFont);
			HFONT hFontOld = (HFONT) SelectObject(hdc, hFont);

			RECT rc = { 0, 0, 0xFFFF, 0xFFFF };

			DrawText(hdc, dict->full_name, lstrlen(dict->full_name), &rc, DT_NOPREFIX | DT_SINGLELINE | DT_CALCRECT);

			lpmis->itemHeight = max(ICON_SIZE, max(bmpChecked.bmHeight, rc.bottom));
			lpmis->itemWidth = 2 + bmpChecked.bmWidth + 2 + ICON_SIZE + 4 + rc.right + 2;

			SelectObject(hdc, hFontOld);
			DeleteObject(hFont);
			ReleaseDC(hwnd, hdc);
			
			return TRUE;
		}
	}

	return CallWindowProc(dlg->old_menu_proc, hwnd, msg, wParam, lParam);
}

TCHAR *lstrtrim(TCHAR *str)
{
	int len = lstrlen(str);

	int i;
	for(i = len - 1; i >= 0 && (str[i] == ' ' || str[i] == '\t'); --i) ;
	if (i < len - 1)
	{
		++i;
		str[i] = _T('\0');
		len = i;
	}

	for(i = 0; i < len && (str[i] == ' ' || str[i] == '\t'); ++i) ;
	if (i > 0)
		memmove(str, &str[i], (len - i + 1) * sizeof(TCHAR));

	return str;
}

BOOL lstreq(TCHAR *a, TCHAR *b, size_t len = -1)
{
#ifdef UNICODE
	a = CharLower(_tcsdup(a));
	b = CharLower(_tcsdup(b));
	BOOL ret;
	if (len >= 0)
		ret = !_tcsncmp(a, b, len);
	else
		ret = !_tcscmp(a, b);
	free(a);
	free(b);
	return ret;
#else
	if (len > 0)
		return !_tcsnicmp(a, b, len);
	else
		return !_tcsicmp(a, b);
#endif
}

struct AddReplacementData
{
	Dictionary *dict;
	TCHAR *word;
	HWND hwndParent;
};

static BOOL CALLBACK DlgProcAddReplacement(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch ( msg )
	{
		case WM_INITDIALOG:
		{
			TranslateDialogDefault(hwndDlg);

			HICON hIcon = LoadIconEx("spellchecker_enabled");
			SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM) hIcon);
			ReleaseIconEx(hIcon);

			SendMessage(GetDlgItem(hwndDlg, IDC_NEW), EM_LIMITTEXT, 256, 0);

			AddReplacementData *data = (AddReplacementData *) lParam;
			SetWindowLong(hwndDlg, GWL_USERDATA, (LONG) data);

			SetDlgItemText(hwndDlg, IDC_OLD, data->word);

			return TRUE;
		}

		case WM_COMMAND:
			switch(wParam)
			{
				case IDOK:
				{
					AddReplacementData *data = (AddReplacementData *) GetWindowLong(hwndDlg, GWL_USERDATA);
					TCHAR tmp[256];
					GetDlgItemText(hwndDlg, IDC_NEW, tmp, sizeof(tmp));
					lstrtrim(tmp);

					if (tmp[0] == '\0')
					{
						MessageBox(hwndDlg, _T("The correction can't be an empty word!"), _T("Wrong Correction"), 
							MB_OK | MB_ICONERROR);
					}
					else if (lstreq(data->word, tmp))
					{
						MessageBox(hwndDlg, _T("The correction can't be the equal to the wrong word!"), _T("Wrong Correction"), 
							MB_OK | MB_ICONERROR);
					}
					else
					{
						data->dict->addToAutoReplace(data->word, tmp);
						if (data->hwndParent != NULL)
							SendMessage(data->hwndParent, WMU_DICT_CHANGED, 0, 0);

						DestroyWindow(hwndDlg);
					}

					break;
				}
				case IDCANCEL:
				{
 					DestroyWindow(hwndDlg);
					break;
				}
			}
			break;

		case WM_CLOSE:
			DestroyWindow(hwndDlg);
			break;

		case WM_DESTROY:
			AddReplacementData *data = (AddReplacementData *) GetWindowLong(hwndDlg, GWL_USERDATA);
			mir_free(data->word);
			free(data);
			break;
	}
	
	return FALSE;
}


void AddReplaceDialog(Dictionary *dict, TCHAR *word, HWND hwndParent) 
{
	AddReplacementData *data = (AddReplacementData *) malloc(sizeof(AddReplacementData));
	data->dict = dict;
	data->word = mir_tstrdup(word);
	data->hwndParent = hwndParent;

	HWND hwnd = CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_ADD_REPLACEMENT), hwndParent, DlgProcAddReplacement, (LPARAM) data);
	SetForegroundWindow(hwnd);
	SetFocus(hwnd);
	ShowWindow(hwnd, SW_SHOW);
}

