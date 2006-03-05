/* 
Copyright (C) 2005 Ricardo Pescuma Domenecci

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


#include "frame.h"
#include "wingdi.h"
#include "winuser.h"


// Prototypes /////////////////////////////////////////////////////////////////////////////////////


#define WINDOW_NAME_PREFIX "mydetails_window"
#define WINDOW_CLASS_NAME "MyDetailsFrame"
#define CONTAINER_CLASS_NAME "MyDetailsFrameContainer"

#define ID_FRAME_TIMER			1011
#define ID_RECALC_TIMER			1012
#define ID_STATUSMESSAGE_TIMER	1013

#define RECALC_TIME				1000

#define IDC_HAND				MAKEINTRESOURCE(32649)

#define DEFAULT_NICKNAME		"<no nickname>"
#define DEFAULT_STATUS_MESSAGE	"<no status message>"


// Messages
#define MWM_REFRESH				(WM_USER+10)
#define MWM_NICK_CHANGED		(WM_USER+11)
#define MWM_STATUS_CHANGED		(WM_USER+12)
#define MWM_STATUS_MSG_CHANGED	(WM_USER+13)
#define MWM_AVATAR_CHANGED		(WM_USER+14)


HWND hwnd_frame = NULL;
HWND hwnd_container = NULL;

int frame_id = -1;

HANDLE hMenuShowHideFrame = 0;


#define FONT_NICK 0
#define FONT_PROTO 1
#define FONT_STATUS 2
#define FONT_AWAY_MSG 3
#define NUM_FONTS 4

FontID font_id[NUM_FONTS];
HFONT hFont[NUM_FONTS];
COLORREF font_colour[NUM_FONTS];

// Defaults
char *font_names[] = { "Nickname", "Protocol", "Status", "Status Message" };
char font_sizes[] = { 13, 8, 8, 8 };
BYTE font_styles[] = { DBFONTF_BOLD, 0, 0, DBFONTF_ITALIC };
COLORREF font_colors[] = { RGB(0,0,0), RGB(0,0,0), RGB(0,0,0), RGB(150,150,150) };


int CreateFrame();
void FixMainMenu();
void RefreshFrame();
void RedrawFrame();


// used when no multiwindow functionality avaiable
bool MyDetailsFrameVisible();
void SetMyDetailsFrameVisible(bool visible);
int ShowHideMenuFunc(WPARAM wParam, LPARAM lParam);



LRESULT CALLBACK FrameContainerWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK FrameWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void SetCicleTime();
void SetCicleTime(HWND hwnd);
void SetStatusMessageRefreshTime();
void SetStatusMessageRefreshTime(HWND hwnd);
int SettingsChangedHook(WPARAM wParam, LPARAM lParam);
int AvatarChangedHook(WPARAM wParam, LPARAM lParam);
int ProtoAckHook(WPARAM wParam, LPARAM lParam);
int SmileyAddOptionsChangedHook(WPARAM wParam,LPARAM lParam);


#define OUTSIDE_BORDER 6
#define SPACE_IMG_TEXT 6
#define SPACE_TEXT_TEXT 0
#define SPACE_ICON_TEXT 2
#define ICON_SIZE 16

#define BORDER_SPACE 2

struct MyDetailsFrameData
{
	RECT proto_rect;
	bool draw_proto;
	bool mouse_over_proto;

	RECT img_rect;
	bool draw_img;
	bool mouse_over_img;

	RECT nick_rect;
	bool draw_nick;
	bool mouse_over_nick;
	HWND nick_tt_hwnd;

	RECT status_rect;
	RECT status_icon_rect;
	RECT status_text_rect;
	bool draw_status;
	bool mouse_over_status;
	HWND status_tt_hwnd;

	RECT away_msg_rect;
	bool draw_away_msg;
	bool mouse_over_away_msg;
	HWND away_msg_tt_hwnd;

	int protocol_number;

	bool showing_menu;

	bool recalc_rectangles;

	bool get_status_messages;
};


// Functions //////////////////////////////////////////////////////////////////////////////////////

void InitFrames()
{
	InitContactListSmileys();

	CreateFrame();

	HookEvent(ME_DB_CONTACT_SETTINGCHANGED, SettingsChangedHook);
	HookEvent(ME_AV_MYAVATARCHANGED, AvatarChangedHook);
	HookEvent(ME_PROTO_ACK, ProtoAckHook);
	HookEvent(ME_SMILEYADD_OPTIONSCHANGED,SmileyAddOptionsChangedHook);
}


void DeInitFrames()
{
	if(ServiceExists(MS_CLIST_FRAMES_REMOVEFRAME) && frame_id != -1) 
	{
		CallService(MS_CLIST_FRAMES_REMOVEFRAME, (WPARAM)frame_id, 0);
	}

	for (int i = 0 ; i < NUM_FONTS ; i++ )
	{
		if (hFont[i] != 0) DeleteObject(hFont[i]);
	}

	if (hwnd_frame != NULL) DestroyWindow(hwnd_frame);
	if (hwnd_container != NULL) DestroyWindow(hwnd_container);
}


int SkinReload(WPARAM wParam, LPARAM lParam) 
{
	CreateGlyphedObjectDefColor(Translate("MyDetails/Backgrnd"), GetSysColor(COLOR_BTNFACE));
	RefreshFrame();
	return 0;
}


int ReloadFont(WPARAM wParam, LPARAM lParam) 
{
	LOGFONT log_font;

	for (int i = 0 ; i < NUM_FONTS ; i++ )
	{
		if (hFont[i] != 0) DeleteObject(hFont[i]);

		font_colour[i] = CallService(MS_FONT_GET, (WPARAM)&font_id[i], (LPARAM)&log_font);
		hFont[i] = CreateFontIndirect(&log_font);
	}
	
	RefreshFrame();
	return 0;
}

int SmileyAddOptionsChangedHook(WPARAM wParam,LPARAM lParam)
{
	RefreshFrame();
	return 0;
}

int CreateFrame() 
{
	if(ServiceExists(MS_FONT_REGISTER)) 
	{
		HDC hdc = GetDC(NULL);

		for (int i = 0 ; i < NUM_FONTS ; i++ )
		{
			ZeroMemory(&font_id[i], sizeof(font_id[i]));

			font_id[i].cbSize = sizeof(FontID);
			strncpy(font_id[i].group, Translate("My Details"), sizeof(font_id[i].group));
			strncpy(font_id[i].name, Translate(font_names[i]), sizeof(font_id[i].name));
			strncpy(font_id[i].dbSettingsGroup, MODULE_NAME, sizeof(font_id[i].dbSettingsGroup));

			char tmp[128];
			mir_snprintf(tmp, sizeof(tmp), "%sFont", font_names[i]);
			strncpy(font_id[i].prefix, tmp, sizeof(font_id[i].prefix));

			font_id[i].deffontsettings.colour = font_colors[i];
			font_id[i].deffontsettings.size = -MulDiv(font_sizes[i], GetDeviceCaps(hdc, LOGPIXELSY), 72);
			font_id[i].deffontsettings.style = font_styles[i];
			font_id[i].deffontsettings.charset = DEFAULT_CHARSET;
			strncpy(font_id[i].deffontsettings.szFace, "Tahoma", sizeof(font_id[i].deffontsettings.szFace));
			font_id[i].order = i;
			font_id[i].flags = FIDF_DEFAULTVALID;

			CallService(MS_FONT_REGISTER, (WPARAM)&font_id[i], 0);
		}

		ReleaseDC(NULL, hdc);

		ReloadFont(0,0);
		HookEvent(ME_FONT_RELOAD, ReloadFont);
	}
	else 
	{
		LOGFONT lf;
		ZeroMemory(&lf, sizeof(lf));
		lf.lfCharSet = DEFAULT_CHARSET;

		HDC hdc = GetDC(NULL);

		for (int i = 0 ; i < NUM_FONTS ; i++ )
		{
			lf.lfHeight = -MulDiv(font_sizes[i], GetDeviceCaps(hdc, LOGPIXELSY), 72);
			lf.lfWeight = font_styles[i] == DBFONTF_BOLD ? FW_BOLD : FW_NORMAL;
			lf.lfItalic = font_styles[i] == DBFONTF_ITALIC ? TRUE : FALSE;
			strncpy(lf.lfFaceName, "Tahoma", sizeof(lf.lfFaceName));
			
			hFont[i] = CreateFontIndirect(&lf);
			font_colour[i] = font_colors[i];
		}
	}


	if(ServiceExists(MS_SKIN_REGISTERDEFOBJECT)) 
	{
		SkinReload(0, 0);
		HookEvent(ME_SKIN_SERVICESCREATED, SkinReload);
	}


	WNDCLASS wndclass;
	wndclass.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW; //CS_PARENTDC | CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc   = FrameWindowProc;
	wndclass.cbClsExtra    = 0;
	wndclass.cbWndExtra    = 0;
	wndclass.hInstance     = hInst;
	wndclass.hIcon         = NULL;
	wndclass.hCursor       = LoadCursor (NULL, IDC_ARROW);
	wndclass.hbrBackground = 0; //(HBRUSH)(COLOR_3DFACE+1);
	wndclass.lpszMenuName  = NULL;
	wndclass.lpszClassName = WINDOW_CLASS_NAME;
	RegisterClass(&wndclass);


	if(ServiceExists(MS_CLIST_FRAMES_ADDFRAME)) 
	{
		hwnd_frame = CreateWindow(WINDOW_CLASS_NAME, Translate("My Details"), 
				WS_CHILD | WS_VISIBLE, 
				0,0,10,10, (HWND)CallService(MS_CLUI_GETHWND, 0, 0), NULL, hInst, NULL);

		CLISTFrame Frame = {0};
		
		Frame.name = Translate("My Details");
		Frame.cbSize = sizeof(CLISTFrame);
		Frame.hWnd = hwnd_frame;
		Frame.align = alTop;
		Frame.Flags = F_VISIBLE | F_SHOWTB | F_SHOWTBTIP | F_NOBORDER;
		Frame.height = 100;

		frame_id = CallService(MS_CLIST_FRAMES_ADDFRAME, (WPARAM)&Frame, 0);
	}
	else 
	{
		wndclass.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;//CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc   = FrameContainerWindowProc;
		wndclass.cbClsExtra    = 0;
		wndclass.cbWndExtra    = 0;
		wndclass.hInstance     = hInst;
		wndclass.hIcon         = NULL;
		wndclass.hCursor       = LoadCursor (NULL, IDC_ARROW);
		wndclass.hbrBackground = 0; //(HBRUSH)(COLOR_3DFACE+1);
		wndclass.lpszMenuName  = NULL;
		wndclass.lpszClassName = CONTAINER_CLASS_NAME;
		RegisterClass(&wndclass);

		hwnd_container = CreateWindowEx(WS_EX_TOOLWINDOW, CONTAINER_CLASS_NAME, Translate("My Details"), 
			(WS_THICKFRAME | WS_CAPTION | WS_SYSMENU) & ~WS_VISIBLE,
			0,0,200,130, (HWND)CallService(MS_CLUI_GETHWND, 0, 0), NULL, hInst, NULL);
	
		hwnd_frame = CreateWindow(WINDOW_CLASS_NAME, Translate("My Details"), 
			WS_CHILD | WS_VISIBLE,
			0,0,10,10, hwnd_container, NULL, hInst, NULL);

		SetWindowLong(hwnd_container, GWL_USERDATA, (LONG)hwnd_frame);
		SendMessage(hwnd_container, WM_SIZE, 0, 0);

		///////////////////////
		// create menu item
		CreateServiceFunction(MODULE_NAME "/ShowHideMyDetails", ShowHideMenuFunc);

		CLISTMENUITEM menu = {0};

		menu.cbSize=sizeof(menu);
		menu.flags = CMIM_ALL;
		menu.popupPosition = -0x7FFFFFFF;
		menu.pszPopupName = Translate("My Details");
		menu.position = 1; // 500010000
		menu.hIcon = LoadSkinnedIcon(SKINICON_OTHER_MIRANDA);
		menu.pszName = Translate("Show My Details");
		menu.pszService= MODULE_NAME "/ShowHideMyDetails";
		hMenuShowHideFrame = (HANDLE)CallService(MS_CLIST_ADDMAINMENUITEM, 0, (LPARAM)&menu);
		/////////////////////


		if(DBGetContactSettingByte(0, MODULE_NAME, SETTING_FRAME_VISIBLE, 1) == 1) 
		{
			ShowWindow(hwnd_container, SW_SHOW);
			FixMainMenu();
		}
	}

	return 0;
}


bool FrameIsFloating() 
{
	if (frame_id == -1) 
	{
		return true; // no frames, always floating
	}
	
	return (CallService(MS_CLIST_FRAMES_GETFRAMEOPTIONS, MAKEWPARAM(FO_FLOATING, frame_id), 0) != 0);
}


LRESULT CALLBACK FrameContainerWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) 
	{
		case WM_SHOWWINDOW:
		{
			if ((BOOL)wParam)
				Utils_RestoreWindowPosition(hwnd, 0, MODULE_NAME, WINDOW_NAME_PREFIX);
			else
				Utils_SaveWindowPosition(hwnd, 0, MODULE_NAME, WINDOW_NAME_PREFIX);
			break;
		}

		case WM_ERASEBKGND:
		{
			HWND child = (HWND)GetWindowLong(hwnd, GWL_USERDATA);

			SendMessage(child, WM_ERASEBKGND, wParam, lParam);
			break;
		}

		case WM_SIZE:
		{
			HWND child = (HWND)GetWindowLong(hwnd, GWL_USERDATA);
			RECT r;
			GetClientRect(hwnd, &r);

			SetWindowPos(child, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
			InvalidateRect(child, NULL, TRUE);

			return TRUE;
		}

		case WM_CLOSE:
		{
			DBWriteContactSettingByte(0, MODULE_NAME, SETTING_FRAME_VISIBLE, 0);
			ShowWindow(hwnd, SW_HIDE);
			FixMainMenu();
			return TRUE;
		}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}



BOOL ScreenToClient(HWND hWnd, LPRECT lpRect)
{
	BOOL ret;

	POINT pt;

	pt.x = lpRect->left;
	pt.y = lpRect->top;

	ret = ScreenToClient(hWnd, &pt);

	if (!ret) return ret;

	lpRect->left = pt.x;
	lpRect->top = pt.y;


	pt.x = lpRect->right;
	pt.y = lpRect->bottom;

	ret = ScreenToClient(hWnd, &pt);

	lpRect->right = pt.x;
	lpRect->bottom = pt.y;

	return ret;
}


BOOL MoveWindow(HWND hWnd, const RECT &rect, BOOL bRepaint)
{
	return MoveWindow(hWnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, bRepaint);
}


RECT GetInnerRect(const RECT &rc, const RECT &clipping)
{
	RECT rc_ret = rc;

	rc_ret.left = max(rc.left, clipping.left);
	rc_ret.top = max(rc.top, clipping.top);
	rc_ret.right = min(rc.right, clipping.right);
	rc_ret.bottom = min(rc.bottom, clipping.bottom);

	return rc_ret;
}


RECT GetRect(HDC hdc, RECT rc, SIZE s, UINT uFormat, int next_top, int text_left, bool frame = true, 
			 bool end_elipsis_on_frame = true)
{
	RECT r = rc;

	if (frame && end_elipsis_on_frame)
	{
		// Add space to ...
		uFormat &= ~DT_END_ELLIPSIS;

		RECT rc_tmp = rc;
		DrawText(hdc, " ...", 4, &rc_tmp, DT_CALCRECT | uFormat);

		s.cx += rc_tmp.right - rc_tmp.left;
	}

	r.top = next_top;
	r.bottom = r.top + s.cy;

	if (opts.draw_text_align_right)
	{
		r.left = r.right - s.cx;
	}
	else
	{
		r.left = text_left;
		r.right = r.left + s.cx;
	}

	if (frame)
	{
		r.bottom += 2 * BORDER_SPACE;

		if (opts.draw_text_align_right)
			r.left -= 2 * BORDER_SPACE;
		else
			r.right += 2 * BORDER_SPACE;
	}

	// Make it fit inside original rc
	r.top = max(next_top, r.top);
 	r.bottom = min(rc.bottom, r.bottom);
	r.left = max(text_left, r.left);
	r.right = min(rc.right, r.right);

	return r;
}

RECT GetRect(HDC hdc, RECT rc, const char *text, const char *def_text, Protocol *proto, UINT uFormat, 
			 int next_top, int text_left, bool smileys = true, bool frame = true, bool end_elipsis_on_frame = true)
{
	const char *tmp;

	if (text[0] == '\0')
		tmp = Translate(def_text);
	else
		tmp = text;

	uFormat &= ~DT_END_ELLIPSIS;

	SIZE s;
	RECT r_tmp = rc;

	// Only first line
	char *tmp2 = strdup(tmp);
	char *pos = strchr(tmp2, '\r');
	if (pos != NULL) pos[0] = '\0';
	pos = strchr(tmp2, '\n');
	if (pos != NULL) pos[0] = '\0';

	if (smileys)
		DRAW_TEXT(hdc, tmp2, strlen(tmp2), &r_tmp, uFormat | DT_CALCRECT, proto->name, NULL);
	else
		DrawText(hdc, tmp2, strlen(tmp2), &r_tmp, uFormat | DT_CALCRECT);

	free(tmp2);

	s.cx = r_tmp.right - r_tmp.left;
	s.cy = r_tmp.bottom - r_tmp.top;

	return GetRect(hdc, rc, s, uFormat, next_top, text_left, frame, end_elipsis_on_frame);
}

HWND CreateTooltip(HWND hwnd, RECT &rect)
{
          // struct specifying control classes to register
    INITCOMMONCONTROLSEX iccex; 
    HWND hwndTT;                 // handle to the ToolTip control
          // struct specifying info about tool in ToolTip control
    TOOLINFO ti;
    unsigned int uid = 0;       // for ti initialization

	// Load the ToolTip class from the DLL.
    iccex.dwSize = sizeof(iccex);
    iccex.dwICC  = ICC_BAR_CLASSES;

    if(!InitCommonControlsEx(&iccex))
       return NULL;

    /* CREATE A TOOLTIP WINDOW */
    hwndTT = CreateWindowEx(WS_EX_TOPMOST,
        TOOLTIPS_CLASS,
        NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,		
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        hwnd,
        NULL,
        hInst,
        NULL
        );

	/* Gives problem with mToolTip
    SetWindowPos(hwndTT,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	*/

    /* INITIALIZE MEMBERS OF THE TOOLINFO STRUCTURE */
    ti.cbSize = sizeof(TOOLINFO);
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = hwnd;
    ti.hinst = hInst;
    ti.uId = uid;
    ti.lpszText = LPSTR_TEXTCALLBACK;
        // ToolTip control will cover the whole window
    ti.rect.left = rect.left;    
    ti.rect.top = rect.top;
    ti.rect.right = rect.right;
    ti.rect.bottom = rect.bottom;

    /* SEND AN ADDTOOL MESSAGE TO THE TOOLTIP CONTROL WINDOW */
    SendMessage(hwndTT, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);	
	SendMessage(hwndTT, TTM_SETDELAYTIME, (WPARAM) (DWORD) TTDT_AUTOPOP, (LPARAM) MAKELONG(24 * 60 * 60 * 1000, 0));	

	return hwndTT;
} 

void DeleteTooltipWindows(MyDetailsFrameData *data)
{
	if (data->nick_tt_hwnd != NULL)
	{ 
		DestroyWindow(data->nick_tt_hwnd);
		data->nick_tt_hwnd = NULL;
	}

	if (data->status_tt_hwnd != NULL)
	{ 
		DestroyWindow(data->status_tt_hwnd);
		data->status_tt_hwnd = NULL;
	}

	if (data->away_msg_tt_hwnd != NULL)
	{ 
		DestroyWindow(data->away_msg_tt_hwnd);
		data->away_msg_tt_hwnd = NULL;
	}
}

void CalcRectangles(HWND hwnd)
{
	HDC hdc = GetDC(hwnd);
	MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);

	if (hdc == NULL || data == NULL)
		return;

	Protocol *proto = protocols->Get(data->protocol_number);

OutputDebugString("CalcRectangles\n");

	data->recalc_rectangles = false;
	proto->data_changed = false;

	data->draw_proto = false;
	data->draw_img = false;
	data->draw_nick = false;
	data->draw_status = false;
	data->draw_away_msg = false;

	DeleteTooltipWindows(data);

	if (ServiceExists(MS_CLIST_FRAMES_SETFRAMEOPTIONS) && frame_id != -1)
	{
		int flags = CallService(MS_CLIST_FRAMES_GETFRAMEOPTIONS, MAKEWPARAM(FO_FLAGS, frame_id), 0);
		if(flags & F_UNCOLLAPSED) 
		{
			RECT rf;
			GetClientRect(hwnd, &rf);

			int size = 0;

			if (rf.bottom - rf.top != size)
			{
				if (FrameIsFloating()) 
				{
					HWND parent = GetParent(hwnd);

					if (parent != NULL)
					{
						RECT rp_client, rp_window, r_window;
						GetClientRect(parent, &rp_client);
						GetWindowRect(parent, &rp_window);
						GetWindowRect(hwnd, &r_window);
						int diff = (rp_window.bottom - rp_window.top) - (rp_client.bottom - rp_client.top);
						if(ServiceExists(MS_CLIST_FRAMES_ADDFRAME))
							diff += (r_window.top - rp_window.top);

						SetWindowPos(parent, 0, 0, 0, rp_window.right - rp_window.left, size + diff, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
					}
				}
			}
			ReleaseDC(hwnd, hdc);
			return;
		}
	}

	RECT r;
	GetClientRect(hwnd, &r);

	if (opts.resize_frame)
		r.bottom = 0x7FFFFFFF;

	int next_top;
	int text_left;
	int avatar_bottom = 0;

	UINT uFormat = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS 
					| (opts.draw_text_align_right ? DT_RIGHT : DT_LEFT)
					| (opts.draw_text_rtl ? DT_RTLREADING : 0);

	// make some borders
	r.left += min(opts.borders[LEFT], r.right);
	r.right = max(r.right - opts.borders[RIGHT], r.left);
	r.top += min(opts.borders[TOP], r.bottom);
	r.bottom = max(r.bottom - opts.borders[BOTTOM], r.top);

	next_top = r.top;
	text_left = r.left;

	//if (r.right > r.left && r.bottom > r.top)
	{
		// Draw image?
		//proto->GetAvatar();
		if (proto->CanGetAvatar())
		{
			if (proto->avatar_bmp != NULL)
			{
				data->draw_img = true;

				BITMAP bmp;
				if (GetObject(proto->avatar_bmp, sizeof(bmp), &bmp))
				{
					// make bounds
					RECT rc = r;

					LONG width;
					LONG height;

					if (opts.draw_avatar_custom_size)
					{
						rc.right = opts.draw_avatar_custom_size_pixels;

						width = opts.draw_avatar_custom_size_pixels;
						height = opts.draw_avatar_custom_size_pixels;
					}
					else if (opts.resize_frame)
					{
						rc.right = rc.left + (rc.right - rc.left) / 3;

						width = rc.right - rc.left;
						height = rc.bottom - rc.top;
					}
					else
					{
						rc.right = rc.left + min((rc.right - rc.left) / 3, rc.bottom - rc.top);

						width = rc.right - rc.left;
						height = rc.bottom - rc.top;
					}

					// Fit to image proportions
					if (!opts.draw_avatar_allow_to_grow)
					{
						if (width > bmp.bmWidth)
							width = bmp.bmWidth;

						if (height > bmp.bmHeight)
							height = bmp.bmHeight;
					}

					if (!opts.resize_frame && height * bmp.bmWidth / bmp.bmHeight <= width)
					{
						width = height * bmp.bmWidth / bmp.bmHeight;
					}
					else
					{
						height = width * bmp.bmHeight / bmp.bmWidth;
					}

					rc.right = rc.left + width;
					rc.bottom = rc.top + height;

					data->img_rect = rc;

					avatar_bottom = data->img_rect.bottom + SPACE_TEXT_TEXT;

					// Make space to nick
					text_left = data->img_rect.right + SPACE_IMG_TEXT;
				}
			}
		}

		// Always draw nick
		{
			data->draw_nick = true;

			SelectObject(hdc, hFont[FONT_NICK]);

			data->nick_rect = GetRect(hdc, r, proto->nickname, DEFAULT_NICKNAME, proto, uFormat, 
									  next_top, text_left);

			if (proto->nickname[0] != '\0')
				data->nick_tt_hwnd = CreateTooltip(hwnd, data->nick_rect);

			next_top = data->nick_rect.bottom + SPACE_TEXT_TEXT;
		}

		// Fits more?
		if (next_top > r.bottom) 
			goto finish;

		if (next_top > avatar_bottom && opts.use_avatar_space_to_draw_text)
			text_left = r.left;

		// Protocol?
		if (opts.draw_show_protocol_name)
		{
			data->draw_proto = true;

			SelectObject(hdc, hFont[FONT_PROTO]);

			data->proto_rect = GetRect(hdc, r, proto->description, "", proto, uFormat, 
									  next_top, text_left, false, true, false);

			next_top = data->proto_rect.bottom + SPACE_TEXT_TEXT;
		}

		// Fits more?
		if (next_top + 2 * BORDER_SPACE > r.bottom) 
			goto finish;

		if (next_top > avatar_bottom && opts.use_avatar_space_to_draw_text)
			text_left = r.left;

		// Status data?
		{
			data->draw_status = true;

			SelectObject(hdc, hFont[FONT_STATUS]);

			// Text size
			RECT r_tmp = r;
			DrawText(hdc, proto->status_name, strlen(proto->status_name), &r_tmp, 
					 DT_CALCRECT | (uFormat & ~DT_END_ELLIPSIS));

			SIZE s;
			s.cy = max(r_tmp.bottom - r_tmp.top, ICON_SIZE);
			s.cx = ICON_SIZE + SPACE_ICON_TEXT + r_tmp.right - r_tmp.left;

			// Status global rect
			data->status_rect = GetRect(hdc, r, s, uFormat, next_top, text_left, true, false);

			if (proto->status_name[0] != '\0')
				data->status_tt_hwnd = CreateTooltip(hwnd, data->status_rect);

			next_top = data->status_rect.bottom + SPACE_TEXT_TEXT;

			RECT rc_inner = data->status_rect;
			rc_inner.top += BORDER_SPACE;
			rc_inner.bottom -= BORDER_SPACE;
			rc_inner.left += BORDER_SPACE;
			rc_inner.right -= BORDER_SPACE;

			// Icon
			data->status_icon_rect = rc_inner;

			if (opts.draw_text_align_right || opts.draw_text_rtl)
				data->status_icon_rect.left = max(data->status_icon_rect.right - ICON_SIZE, rc_inner.left);
			else
				data->status_icon_rect.right = min(data->status_icon_rect.left + ICON_SIZE, rc_inner.right);

			if (r_tmp.bottom - r_tmp.top > ICON_SIZE)
			{
				data->status_icon_rect.top += (r_tmp.bottom - r_tmp.top - ICON_SIZE) / 2;
				data->status_icon_rect.bottom = data->status_icon_rect.top + ICON_SIZE;
			}

			// Text
			data->status_text_rect = GetInnerRect(rc_inner, r);

			if (opts.draw_text_align_right || opts.draw_text_rtl)
				data->status_text_rect.right = max(data->status_icon_rect.left - SPACE_ICON_TEXT, rc_inner.left);
			else
				data->status_text_rect.left = min(data->status_icon_rect.right + SPACE_ICON_TEXT, rc_inner.right);

			if (ICON_SIZE > r_tmp.bottom - r_tmp.top)
			{
				data->status_text_rect.top += (ICON_SIZE - (r_tmp.bottom - r_tmp.top)) / 2;
				data->status_text_rect.bottom = data->status_text_rect.top + r_tmp.bottom - r_tmp.top;
			}
		}

		// Fits more?
		if (next_top + 2 * BORDER_SPACE > r.bottom) 
			goto finish;

		if (next_top > avatar_bottom && opts.use_avatar_space_to_draw_text)
			text_left = r.left;

		// Away msg?
		if(proto->CanGetStatusMsg()) 
		{
			data->draw_away_msg = true;

			SelectObject(hdc, hFont[FONT_AWAY_MSG]);

			data->away_msg_rect = GetRect(hdc, r, proto->status_message, DEFAULT_STATUS_MESSAGE, proto, uFormat, 
						  next_top, text_left);

			if (proto->status_message[0] != '\0')
				data->away_msg_tt_hwnd = CreateTooltip(hwnd, data->away_msg_rect);

			next_top = data->away_msg_rect.bottom + SPACE_TEXT_TEXT;
		}
	}

	r.bottom = max(next_top - SPACE_TEXT_TEXT, avatar_bottom);

	if (opts.resize_frame && ServiceExists(MS_CLIST_FRAMES_SETFRAMEOPTIONS) && frame_id != -1)
	{
		RECT rf;
		GetClientRect(hwnd, &rf);

		int size = r.bottom + opts.borders[BOTTOM];

		if (rf.bottom - rf.top != size)
		{
			if (FrameIsFloating()) 
			{
				HWND parent = GetParent(hwnd);

				if (parent != NULL)
				{
					RECT rp_client, rp_window, r_window;
					GetClientRect(parent, &rp_client);
					GetWindowRect(parent, &rp_window);
					GetWindowRect(hwnd, &r_window);
					int diff = (rp_window.bottom - rp_window.top) - (rp_client.bottom - rp_client.top);
					if(ServiceExists(MS_CLIST_FRAMES_ADDFRAME))
						diff += (r_window.top - rp_window.top);

					SetWindowPos(parent, 0, 0, 0, rp_window.right - rp_window.left, size + diff, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
				}
			}
			else if (IsWindowVisible(hwnd) && ServiceExists(MS_CLIST_FRAMES_ADDFRAME)) 
			{
				int flags = CallService(MS_CLIST_FRAMES_GETFRAMEOPTIONS, MAKEWPARAM(FO_FLAGS, frame_id), 0);
				if(flags & F_VISIBLE) 
				{
					CallService(MS_CLIST_FRAMES_SETFRAMEOPTIONS, MAKEWPARAM(FO_HEIGHT, frame_id), (LPARAM)(size));
					CallService(MS_CLIST_FRAMES_UPDATEFRAME, (WPARAM)frame_id, (LPARAM)(FU_TBREDRAW | FU_FMREDRAW | FU_FMPOS));
				}
			}
		}
	}

finish:
	ReleaseDC(hwnd, hdc);
}


HBITMAP CreateBitmap32(int cx, int cy)
{
   BITMAPINFO RGB32BitsBITMAPINFO; 
    UINT * ptPixels;
    HBITMAP DirectBitmap;

    ZeroMemory(&RGB32BitsBITMAPINFO,sizeof(BITMAPINFO));
    RGB32BitsBITMAPINFO.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    RGB32BitsBITMAPINFO.bmiHeader.biWidth=cx;//bm.bmWidth;
    RGB32BitsBITMAPINFO.bmiHeader.biHeight=cy;//bm.bmHeight;
    RGB32BitsBITMAPINFO.bmiHeader.biPlanes=1;
    RGB32BitsBITMAPINFO.bmiHeader.biBitCount=32;

    DirectBitmap = CreateDIBSection(NULL, 
                                       (BITMAPINFO *)&RGB32BitsBITMAPINFO, 
                                       DIB_RGB_COLORS,
                                       (void **)&ptPixels, 
                                       NULL, 0);
    return DirectBitmap;
}

void EraseBackground(HWND hwnd, HDC hdc)
{
	RECT r;
	GetClientRect(hwnd, &r);

	if(ServiceExists(MS_SKIN_DRAWGLYPH)) 
	{
		if(FrameIsFloating()) 
		{
			SkinDrawGlyph(hdc, &r, &r, Translate("Main Window/Backgrnd"));
		}
		else 
		{
			SkinDrawWindowBack(hwnd, hdc, &r, Translate("Main Window/Backgrnd"));
		}
		SkinDrawGlyph(hdc, &r, &r, Translate("MyDetails/Backgrnd"));
	}
	else 
	{
		HBRUSH hB = CreateSolidBrush((COLORREF) DBGetContactSettingDword(NULL,"MyDetails","BackgroundColor",GetSysColor(COLOR_BTNFACE)));
		FillRect(hdc, &r, hB);
		DeleteObject(hB);
	}
}

void DrawTextWithRect(HDC hdc, const char *text, const char *def_text, RECT rc, UINT uFormat, 
					  bool mouse_over, Protocol *proto, bool replace_smileys = true)
{
	const char *tmp;

	if (text[0] == '\0')
		tmp = Translate(def_text);
	else
		tmp = text;

	// Only first line
	char *tmp2 = strdup(tmp);
	char *pos = strchr(tmp2, '\r');
	if (pos != NULL) pos[0] = '\0';
	pos = strchr(tmp2, '\n');
	if (pos != NULL) pos[0] = '\0';


	RECT r = rc;
	r.top += BORDER_SPACE;
	r.bottom -= BORDER_SPACE;
	r.left += BORDER_SPACE;
	r.right -= BORDER_SPACE;

	HRGN rgn = CreateRectRgnIndirect(&r);
	SelectClipRgn(hdc, rgn);
	
	RECT rc_tmp;
	int text_height;

	if (mouse_over)
	{
		uFormat &= ~DT_END_ELLIPSIS;

		rc_tmp = r;
		text_height = DrawText(hdc, " ...", 4, &rc_tmp, DT_CALCRECT | uFormat);
		rc_tmp.top += (r.bottom - r.top - text_height) >> 1;
		rc_tmp.bottom = rc_tmp.top + text_height;

		if (uFormat & DT_RTLREADING)
		{
			rc_tmp.right = r.left + (rc_tmp.right - rc_tmp.left);
			rc_tmp.left = r.left;

			r.left += rc_tmp.right - rc_tmp.left;
		}
		else
		{
			rc_tmp.left = r.right - (rc_tmp.right - rc_tmp.left);
			rc_tmp.right = r.right;

			r.right -= rc_tmp.right - rc_tmp.left;
		}
	}

	if (replace_smileys)
		DRAW_TEXT(hdc, tmp2, strlen(tmp2), &r, uFormat, proto->name, NULL);
	else
		DrawText(hdc, tmp2, strlen(tmp2), &r, uFormat);

	if (mouse_over)
	{
		DrawText(hdc, " ...", 4, &rc_tmp, uFormat);
	}

	SelectClipRgn(hdc, NULL);
	DeleteObject(rgn);

	if (mouse_over)
		FrameRect(hdc, &rc, (HBRUSH) GetStockObject(GRAY_BRUSH));

	free(tmp2);
}

void Draw(HWND hwnd, HDC hdc_orig)
{
	MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
	Protocol *proto = protocols->Get(data->protocol_number);

	if (data->recalc_rectangles || proto->data_changed)
		CalcRectangles(hwnd);

	RECT r_full;
	GetClientRect(hwnd, &r_full);
	RECT r = r_full;

	HDC hdc = CreateCompatibleDC(hdc_orig);
	HBITMAP hBmp = CreateBitmap32(r.right,r.bottom);//,1,GetDeviceCaps(hdc,BITSPIXEL),NULL);
	SelectObject(hdc, hBmp);

	int old_bk_mode = SetBkMode(hdc, TRANSPARENT);
	HFONT old_font = (HFONT) SelectObject(hdc, hFont[0]);
	COLORREF old_color = GetTextColor(hdc);
	SetStretchBltMode(hdc, HALFTONE);

	// Erase
	EraseBackground(hwnd, hdc);

	r.left += min(opts.borders[LEFT], r.right);
	r.right = max(r.right - opts.borders[RIGHT], r.left);
	r.top += min(opts.borders[TOP], r.bottom);
	r.bottom = max(r.bottom - opts.borders[BOTTOM], r.top);

	// Draw itens

	UINT uFormat = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS 
					| (opts.draw_text_align_right ? DT_RIGHT : DT_LEFT)
					| (opts.draw_text_rtl ? DT_RTLREADING : 0);

	// Image
	if (data->draw_img)
	{
		RECT rc = GetInnerRect(data->img_rect, r);
		HRGN rgn = CreateRectRgnIndirect(&rc);
		SelectClipRgn(hdc, rgn);

		int width = data->img_rect.right - data->img_rect.left;
		int height = data->img_rect.bottom - data->img_rect.top;

		int round_radius;
		if (opts.draw_avatar_round_corner)
		{
			if (opts.draw_avatar_use_custom_corner_size)
				round_radius = opts.draw_avatar_custom_corner_size;
			else
				round_radius = min(width, height) / 6;
		}
		else
		{
			round_radius = 0;
		}


		AVATARDRAWREQUEST adr = {0};

		adr.cbSize = sizeof(AVATARDRAWREQUEST);
		adr.hTargetDC = hdc;
		adr.rcDraw = data->img_rect;

		adr.dwFlags = AVDRQ_OWNPIC | AVDRQ_HIDEBORDERONTRANSPARENCY | 
			(opts.draw_avatar_border ? AVDRQ_DRAWBORDER : 0 ) |
			(opts.draw_avatar_round_corner ? AVDRQ_ROUNDEDCORNER : 0 );
		adr.clrBorder =  opts.draw_avatar_border_color;
		adr.radius = round_radius;
		adr.alpha = 255;
		adr.szProto = proto->name;

		CallService(MS_AV_DRAWAVATAR, 0, (LPARAM) &adr);

		// Clipping rgn
		SelectClipRgn(hdc, NULL);
		DeleteObject(rgn);
	}

	// Nick
	if (data->draw_nick)
	{
		RECT rc = GetInnerRect(data->nick_rect, r);
		HRGN rgn = CreateRectRgnIndirect(&rc);
		SelectClipRgn(hdc, rgn);

		SelectObject(hdc, hFont[FONT_NICK]);
		SetTextColor(hdc, font_colour[FONT_NICK]);

		DrawTextWithRect(hdc, proto->nickname, DEFAULT_NICKNAME, rc, uFormat, 
						 data->mouse_over_nick && proto->CanSetNick(), proto);

		// Clipping rgn
		SelectClipRgn(hdc, NULL);
		DeleteObject(rgn);
	}

	// Protocol
	if (data->draw_proto)
	{
		RECT rc = GetInnerRect(data->proto_rect, r);
		RECT rr = rc;
		rr.top += BORDER_SPACE;
		rr.bottom -= BORDER_SPACE;
		rr.left += BORDER_SPACE;
		rr.right -= BORDER_SPACE;

		HRGN rgn = CreateRectRgnIndirect(&rc);
		SelectClipRgn(hdc, rgn);

		SelectObject(hdc, hFont[FONT_PROTO]);
		SetTextColor(hdc, font_colour[FONT_PROTO]);

		DrawText(hdc, proto->description, strlen(proto->description), &rr, uFormat);

		// Clipping rgn
		SelectClipRgn(hdc, NULL);
		DeleteObject(rgn);

		if (data->mouse_over_proto)
			FrameRect(hdc, &rc, (HBRUSH) GetStockObject(GRAY_BRUSH));
	}

	// Status
	if (data->draw_status)
	{
		RECT rtmp = GetInnerRect(data->status_rect, r);
		RECT rr = rtmp;
		rr.top += BORDER_SPACE;
		rr.bottom -= BORDER_SPACE;
		rr.left += BORDER_SPACE;
		rr.right -= BORDER_SPACE;

		RECT rc = GetInnerRect(data->status_icon_rect, rr);
		HRGN rgn = CreateRectRgnIndirect(&rc);
		SelectClipRgn(hdc, rgn);

		HICON status_icon;
		if (proto->custom_status != 0 && ProtoServiceExists(proto->name, PS_ICQ_GETCUSTOMSTATUSICON))
		{
			status_icon = (HICON) CallProtoService(proto->name, PS_ICQ_GETCUSTOMSTATUSICON, proto->custom_status, 0);
		}
		else
		{
			status_icon = LoadSkinnedProtoIcon(proto->name, proto->status);
		}
		if (status_icon != NULL)
		{
			DrawIconEx(hdc, data->status_icon_rect.left, data->status_icon_rect.top, status_icon, 
						ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);
			DeleteObject(status_icon);
		}
		
		SelectClipRgn(hdc, NULL);
		DeleteObject(rgn);

		rc = GetInnerRect(data->status_text_rect, rr);
		rgn = CreateRectRgnIndirect(&rc);
		SelectClipRgn(hdc, rgn);

		SelectObject(hdc, hFont[FONT_STATUS]);
		SetTextColor(hdc, font_colour[FONT_STATUS]);

		DrawText(hdc, proto->status_name, strlen(proto->status_name), &rc, uFormat);

		SelectClipRgn(hdc, NULL);
		DeleteObject(rgn);

		if (data->mouse_over_status)
			FrameRect(hdc, &rtmp, (HBRUSH) GetStockObject(GRAY_BRUSH));
	}

	// Away message
	if (data->draw_away_msg)
	{
		RECT rc = GetInnerRect(data->away_msg_rect, r);
		HRGN rgn = CreateRectRgnIndirect(&rc);
		SelectClipRgn(hdc, rgn);

		SelectObject(hdc, hFont[FONT_AWAY_MSG]);
		SetTextColor(hdc, font_colour[FONT_AWAY_MSG]);

		DrawTextWithRect(hdc, proto->status_message, DEFAULT_STATUS_MESSAGE, rc, uFormat, 
						 data->mouse_over_away_msg && proto->CanSetStatusMsg(), proto);

		// Clipping rgn
		SelectClipRgn(hdc, NULL);
		DeleteObject(rgn);
	}

	SelectObject(hdc, old_font);
	SetTextColor(hdc, old_color);
	SetBkMode(hdc, old_bk_mode);

	BitBlt(hdc_orig, r_full.left, r_full.top, r_full.right - r_full.left, 
			r_full.bottom - r_full.top, hdc, r_full.left, r_full.top, SRCCOPY);
	DeleteDC(hdc);
	DeleteObject(hBmp);
}

bool InsideRect(POINT *p, RECT *r)
{
	return p->x >= r->left && p->x < r->right && p->y >= r->top && p->y < r->bottom;
}

void MakeHover(HWND hwnd, bool draw, bool *hover, POINT *p, RECT *r)
{
	if (draw && p != NULL && r != NULL && InsideRect(p, r))
	{
		if (!*hover)
		{
			*hover = true;

			InvalidateRect(hwnd, NULL, FALSE);

			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hwnd;
			tme.dwHoverTime = HOVER_DEFAULT;
			TrackMouseEvent(&tme);
		}
	}
	else 
	{
		if (*hover)
		{
			*hover = false;

			InvalidateRect(hwnd, NULL, FALSE);
		}
	}
}

void ShowGlobalStatusMenu(HWND hwnd, MyDetailsFrameData *data, Protocol *proto, POINT &p)
{
	HMENU submenu = (HMENU) CallService(MS_CLIST_MENUGETSTATUS,0,0);
	
	if (opts.draw_text_align_right)
		p.x = data->status_rect.right;
	else
		p.x = data->status_rect.left;
	p.y =  data->status_rect.bottom+1;
	ClientToScreen(hwnd, &p);
	
	int ret = TrackPopupMenu(submenu, TPM_TOPALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD
			| (opts.draw_text_align_right ? TPM_RIGHTALIGN : TPM_LEFTALIGN), p.x, p.y, 0, hwnd, NULL);

	if(ret)
		CallService(MS_CLIST_MENUPROCESSCOMMAND, MAKEWPARAM(LOWORD(ret),MPCF_MAINMENU),(LPARAM)NULL);
}

void ShowProtocolStatusMenu(HWND hwnd, MyDetailsFrameData *data, Protocol *proto, POINT &p)
{
	HMENU menu = (HMENU) CallService(MS_CLIST_MENUGETSTATUS,0,0);
	HMENU submenu = NULL;

	if (menu != NULL)
	{
		// Find the correct menu item
		int count = GetMenuItemCount(menu);
		for (int i = 0 ; i < count && submenu == NULL; i++)
		{
			MENUITEMINFO mii = {0};

			mii.cbSize = sizeof(mii);

			if(!IsWinVer98Plus()) 
			{
				mii.fMask = MIIM_TYPE;
			}
			else 
			{
				mii.fMask = MIIM_STRING;
			}

			GetMenuItemInfo(menu, i, TRUE, &mii);

			if (mii.cch != 0)
			{
				mii.cch++;
				mii.dwTypeData = (char *)malloc(sizeof(char) * mii.cch);
				GetMenuItemInfo(menu, i, TRUE, &mii);

				if (strcmp(mii.dwTypeData, proto->description) == 0)
				{
					submenu = GetSubMenu(menu, i);
				}

				free(mii.dwTypeData);
			}
		}

		if (submenu == NULL && protocols->GetSize() == 1)
		{
			submenu = menu;
		}
	}

	if (submenu != NULL)
	{
		/*
		// Remove the first itens (protocol name and separator)
		int to_remove = 0;
		for(; to_remove < 5; to_remove++)
		{
			MENUITEMINFO mii = {0};
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_TYPE;
			GetMenuItemInfo(submenu, to_remove, TRUE, &mii);

			if (mii.fType == MFT_SEPARATOR)
				break;
		}

		if (to_remove < 5)
		{
			submenu = CopyMenu(submenu);

			for(int i = 0; i <= to_remove; i++)
				RemoveMenu(submenu, i, MF_BYPOSITION);
		}
		*/

		if (opts.draw_text_align_right)
			p.x = data->status_rect.right;
		else
			p.x = data->status_rect.left;
		p.y =  data->status_rect.bottom+1;
		ClientToScreen(hwnd, &p);
		
		int ret = TrackPopupMenu(submenu, TPM_TOPALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD
				| (opts.draw_text_align_right ? TPM_RIGHTALIGN : TPM_LEFTALIGN), p.x, p.y, 0, hwnd, NULL);

		if(ret)
			CallService(MS_CLIST_MENUPROCESSCOMMAND, MAKEWPARAM(LOWORD(ret),MPCF_MAINMENU),(LPARAM)NULL);

		/*
		if (to_remove < 5)
		{
			DestroyMenu(submenu);
		}
		*/
	}
	else
	{
		// Well, lets do it by hand
		static int statusModePf2List[]={0xFFFFFFFF,PF2_ONLINE,PF2_SHORTAWAY,PF2_LONGAWAY,PF2_LIGHTDND,PF2_HEAVYDND,PF2_FREECHAT,PF2_INVISIBLE,PF2_ONTHEPHONE,PF2_OUTTOLUNCH};

		menu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_MENU1));
		submenu = GetSubMenu(menu, 0);
		CallService(MS_LANGPACK_TRANSLATEMENU,(WPARAM)submenu,0);

		DWORD flags = CallProtoService(proto->name, PS_GETCAPS, PFLAGNUM_2,0);
		for ( int i = GetMenuItemCount(submenu) -1  ; i >= 0 ; i-- )
		{
			if (!(flags & statusModePf2List[i]))
			{
				// Hide menu
				RemoveMenu(submenu, i, MF_BYPOSITION);
			}
		}

		if (opts.draw_text_align_right)
			p.x = data->status_rect.right;
		else
			p.x = data->status_rect.left;
		p.y =  data->status_rect.bottom+1;
		ClientToScreen(hwnd, &p);
		
		int ret = TrackPopupMenu(submenu, TPM_TOPALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD
				| (opts.draw_text_align_right ? TPM_RIGHTALIGN : TPM_LEFTALIGN), p.x, p.y, 0, hwnd, NULL);
		DestroyMenu(menu);
		if(ret) 
		{
			proto->SetStatus(ret);
		}
	}
}

LRESULT CALLBACK FrameWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	switch(msg) 
	{
		case WM_CREATE: 
		{
			MyDetailsFrameData *data = new MyDetailsFrameData();
			ZeroMemory(data, sizeof(MyDetailsFrameData));
			SetWindowLong(hwnd, GWL_USERDATA, (LONG) data);

			data->recalc_rectangles = true;
			data->get_status_messages = false;
			data->showing_menu = false;

			data->protocol_number = DBGetContactSettingWord(NULL,"MyDetails","ProtocolNumber",0);
			if (data->protocol_number >= protocols->GetSize())
			{
				data->protocol_number = 0;
			}

			SetCicleTime(hwnd);
			SetStatusMessageRefreshTime(hwnd);

			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_HOVER | TME_LEAVE;
			tme.hwndTrack = hwnd;
			tme.dwHoverTime = HOVER_DEFAULT;
			TrackMouseEvent(&tme);

			return TRUE;
		}

		/*
		case WM_ERASEBKGND:
		{
			//EraseBackground(hwnd, (HDC)wParam); 
			Draw(hwnd, (HDC)wParam); 
			return TRUE;
		}
		*/

		case WM_PRINTCLIENT:
		{
			Draw(hwnd, (HDC)wParam);
			return TRUE;
		}

		case WM_PAINT:
		{
			RECT r;

			if(GetUpdateRect(hwnd, &r, FALSE)) 
			{
				PAINTSTRUCT ps;

				HDC hdc = BeginPaint(hwnd, &ps);
				Draw(hwnd, hdc);
				EndPaint(hwnd, &ps);
			}
			
			return TRUE;
		}

		case WM_SIZE:
		{
			//InvalidateRect(hwnd, NULL, FALSE);
			MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
			data->recalc_rectangles = true;
			RedrawFrame();
			break;
		}

		case WM_TIMER:
		{
			MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);

			if (wParam == ID_FRAME_TIMER)
			{
				if (!data->showing_menu)
					CallService(MS_MYDETAILS_SHOWNEXTPROTOCOL, 0, 0);
			}
			else if (wParam == ID_RECALC_TIMER)
			{
				KillTimer(hwnd, ID_RECALC_TIMER);

				if (data->get_status_messages)
				{
					SetStatusMessageRefreshTime(hwnd);
					data->get_status_messages = false;

					protocols->GetStatuses();
					protocols->GetStatusMsgs();
				}

				RedrawFrame();
			}
			else if (wParam == ID_STATUSMESSAGE_TIMER)
			{
				SetStatusMessageRefreshTime(hwnd);

				PostMessage(hwnd, MWM_STATUS_MSG_CHANGED, 0, 0);
			}

			return TRUE;
		}

		case WM_LBUTTONUP:
		{
			MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
			Protocol *proto = protocols->Get(data->protocol_number);

			POINT p;
			p.x = LOWORD(lParam); 
			p.y = HIWORD(lParam); 

			// In image?
			if (data->draw_img && InsideRect(&p, &data->img_rect) && proto->CanSetAvatar())
			{
				if (opts.global_on_avatar)
					CallService(MS_MYDETAILS_SETMYAVATARUI, 0, 0);
				else
					CallService(MS_MYDETAILS_SETMYAVATARUI, 0, (LPARAM) proto->name);
			}
			// In nick?
			else if (data->draw_nick && InsideRect(&p, &data->nick_rect) && proto->CanSetNick())
			{
				if (opts.global_on_nickname)
					CallService(MS_MYDETAILS_SETMYNICKNAMEUI, 0, 0);
				else
					CallService(MS_MYDETAILS_SETMYNICKNAMEUI, 0, (LPARAM) proto->name);
			}
			// In status message?
			else if (data->draw_away_msg && InsideRect(&p, &data->away_msg_rect) && proto->CanSetStatusMsg())
			{
				if (opts.global_on_status_message)
					CallService(MS_MYDETAILS_SETMYSTATUSMESSAGEUI, 0, 0);
				else
					CallService(MS_MYDETAILS_SETMYSTATUSMESSAGEUI, 0, (LPARAM) proto->name);
			}
			// In status?
			else if (data->draw_status && InsideRect(&p, &data->status_rect))
			{
				data->showing_menu = true;

				if (opts.global_on_status)
					ShowGlobalStatusMenu(hwnd, data, proto, p);
				else
					ShowProtocolStatusMenu(hwnd, data, proto, p);

				data->showing_menu = false;
			}
			// In protocol?
			else if (data->draw_proto && InsideRect(&p, &data->proto_rect))
			{
				data->showing_menu = true;

				HMENU menu = CreatePopupMenu();

				for (int i = 0 ; i < protocols->GetSize() ; i++)
				{
					MENUITEMINFO mii = {0};
					mii.cbSize = sizeof(mii);
					mii.fMask = MIIM_ID | MIIM_TYPE;
					mii.fType = MFT_STRING;
					mii.dwTypeData = protocols->Get(i)->description;
					mii.cch = strlen(protocols->Get(i)->description);
					mii.wID = i + 1;

					if (i == data->protocol_number)
					{
						mii.fMask |= MIIM_STATE;
						mii.fState = MFS_DISABLED;
					}

					InsertMenuItem(menu, 0, TRUE, &mii);
				}
				
				if (opts.draw_text_align_right)
					p.x = data->proto_rect.right;
				else
					p.x = data->proto_rect.left;
				p.y =  data->proto_rect.bottom+1;
				ClientToScreen(hwnd, &p);
	
				int ret = TrackPopupMenu(menu, TPM_TOPALIGN|TPM_LEFTALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL);
				DestroyMenu(menu);

				if (ret != 0)
				{
					PluginCommand_ShowProtocol(NULL, (WPARAM) protocols->Get(ret-1)->name);
				}

				data->showing_menu = false;
			}

			break;
		}

		case WM_MEASUREITEM:
		{
			return CallService(MS_CLIST_MENUMEASUREITEM,wParam,lParam);
		}
		case WM_DRAWITEM:
		{
			return CallService(MS_CLIST_MENUDRAWITEM,wParam,lParam);
		}

		case WM_CONTEXTMENU:
		{
			MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
			Protocol *proto = protocols->Get(data->protocol_number);

			POINT p;
			p.x = LOWORD(lParam); 
			p.y = HIWORD(lParam); 

			ScreenToClient(hwnd, &p);

			data->showing_menu = true;

			// In image?
			if (data->draw_img && InsideRect(&p, &data->img_rect))
			{
				HMENU menu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_MENU1));
				HMENU submenu = GetSubMenu(menu, 4);
				CallService(MS_LANGPACK_TRANSLATEMENU,(WPARAM)submenu,0);

				// Add this proto to menu
				char tmp[128];
				mir_snprintf(tmp, sizeof(tmp), Translate("Set My Avatar for %s..."), proto->description);

				MENUITEMINFO mii = {0};
				mii.cbSize = sizeof(mii);
				mii.fMask = MIIM_ID | MIIM_TYPE;
				mii.fType = MFT_STRING;
				mii.dwTypeData = tmp;
				mii.cch = strlen(tmp);
				mii.wID = 1;

				if (!proto->CanSetAvatar())
				{
					mii.fMask |= MIIM_STATE;
					mii.fState = MFS_DISABLED;
				}

				InsertMenuItem(submenu, 0, TRUE, &mii);
				
				ClientToScreen(hwnd, &p);
	
				int ret = TrackPopupMenu(submenu, TPM_TOPALIGN|TPM_LEFTALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL);
				DestroyMenu(menu);

				switch(ret)
				{
					case 1:
					{
						CallService(MS_MYDETAILS_SETMYAVATARUI, 0, (LPARAM) proto->name);
						break;
					}
					case ID_AVATARPOPUP_SETMYAVATAR:
					{
						CallService(MS_MYDETAILS_SETMYAVATARUI, 0, 0);
						break;
					}
				}
			}
			// In nick?
			else if (data->draw_nick && InsideRect(&p, &data->nick_rect))
			{
				HMENU menu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_MENU1));
				HMENU submenu = GetSubMenu(menu, 2);
				CallService(MS_LANGPACK_TRANSLATEMENU,(WPARAM)submenu,0);

				// Add this proto to menu
				char tmp[128];
				mir_snprintf(tmp, sizeof(tmp), Translate("Set My Nickname for %s..."), proto->description);

				MENUITEMINFO mii = {0};
				mii.cbSize = sizeof(mii);
				mii.fMask = MIIM_ID | MIIM_TYPE;
				mii.fType = MFT_STRING;
				mii.dwTypeData = tmp;
				mii.cch = strlen(tmp);
				mii.wID = 1;

				if (!proto->CanSetNick())
				{
					mii.fMask |= MIIM_STATE;
					mii.fState = MFS_DISABLED;
				}

				InsertMenuItem(submenu, 0, TRUE, &mii);
				
				ClientToScreen(hwnd, &p);
	
				int ret = TrackPopupMenu(submenu, TPM_TOPALIGN|TPM_LEFTALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL);
				DestroyMenu(menu);

				switch(ret)
				{
					case 1:
					{
						CallService(MS_MYDETAILS_SETMYNICKNAMEUI, 0, (LPARAM) proto->name);
						break;
					}
					case ID_NICKPOPUP_SETMYNICKNAME:
					{
						CallService(MS_MYDETAILS_SETMYNICKNAMEUI, 0, 0);
						break;
					}
				}
			}
			// In status message?
			else if (data->draw_away_msg && InsideRect(&p, &data->away_msg_rect))
			{
				char tmp[128];

				HMENU menu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_MENU1));
				HMENU submenu = GetSubMenu(menu, 3);
				CallService(MS_LANGPACK_TRANSLATEMENU,(WPARAM)submenu,0);

				if (protocols->CanSetStatusMsgPerProtocol())
				{
					// Add this proto to menu
					mir_snprintf(tmp, sizeof(tmp), Translate("Set My Status Message for %s..."), proto->description);

					MENUITEMINFO mii = {0};
					mii.cbSize = sizeof(mii);
					mii.fMask = MIIM_ID | MIIM_TYPE;
					mii.fType = MFT_STRING;
					mii.dwTypeData = tmp;
					mii.cch = strlen(tmp);
					mii.wID = 1;

					if (!proto->CanSetStatusMsg())
					{
						mii.fMask |= MIIM_STATE;
						mii.fState = MFS_DISABLED;
					}

					InsertMenuItem(submenu, 0, TRUE, &mii);;
				}
				else
				{
					// Add this to menu
					mir_snprintf(tmp, sizeof(tmp), Translate("Set My Status Message for %s..."), 
								 CallService(MS_CLIST_GETSTATUSMODEDESCRIPTION, proto->status, 0));

					MENUITEMINFO mii = {0};
					mii.cbSize = sizeof(mii);
					mii.fMask = MIIM_ID | MIIM_TYPE;
					mii.fType = MFT_STRING;
					mii.dwTypeData = tmp;
					mii.cch = strlen(tmp);
					mii.wID = 1;

					InsertMenuItem(submenu, 0, TRUE, &mii);
				}
				
				ClientToScreen(hwnd, &p);
	
				int ret = TrackPopupMenu(submenu, TPM_TOPALIGN|TPM_LEFTALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL);
				DestroyMenu(menu);

				switch(ret)
				{
					case 1:
					{
						CallService(MS_MYDETAILS_SETMYSTATUSMESSAGEUI, 0, (LPARAM) proto->name);
						break;
					}
					case ID_STATUSMESSAGEPOPUP_SETMYSTATUSMESSAGE:
					{
						CallService(MS_MYDETAILS_SETMYSTATUSMESSAGEUI, 0, 0);
						break;
					}
				}
			}
			// In status?
			else if (data->draw_status && InsideRect(&p, &data->status_rect))
			{
				if (opts.global_on_status)
					ShowProtocolStatusMenu(hwnd, data, proto, p);
				else
					ShowGlobalStatusMenu(hwnd, data, proto, p);
			}
			// In protocol?
			else if (data->draw_proto && InsideRect(&p, &data->proto_rect))
			{
			}
			// Default context menu
			else 
			{
				HMENU menu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_MENU1));
				HMENU submenu = GetSubMenu(menu, 1);
				CallService(MS_LANGPACK_TRANSLATEMENU,(WPARAM)submenu,0);

				if (opts.cicle_throught_protocols)
					RemoveMenu(submenu, ID_CICLE_THORUGHT_PROTOS, MF_BYCOMMAND);
				else
					RemoveMenu(submenu, ID_DONT_CICLE_THORUGHT_PROTOS, MF_BYCOMMAND);

				// Add this proto to menu
				char tmp[128];
				MENUITEMINFO mii = {0};

				if (protocols->CanSetStatusMsgPerProtocol())
				{
					// Add this proto to menu
					mir_snprintf(tmp, sizeof(tmp), Translate("Set My Status Message for %s..."), proto->description);

					ZeroMemory(&mii, sizeof(mii));
					mii.cbSize = sizeof(mii);
					mii.fMask = MIIM_ID | MIIM_TYPE;
					mii.fType = MFT_STRING;
					mii.dwTypeData = tmp;
					mii.cch = strlen(tmp);
					mii.wID = 3;

					if (!proto->CanSetStatusMsg())
					{
						mii.fMask |= MIIM_STATE;
						mii.fState = MFS_DISABLED;
					}

					InsertMenuItem(submenu, 0, TRUE, &mii);;
				}
				else
				{
					// Add this to menu
					mir_snprintf(tmp, sizeof(tmp), Translate("Set My Status Message for %s..."), 
								 CallService(MS_CLIST_GETSTATUSMODEDESCRIPTION, proto->status, 0));

					ZeroMemory(&mii, sizeof(mii));
					mii.cbSize = sizeof(mii);
					mii.fMask = MIIM_ID | MIIM_TYPE;
					mii.fType = MFT_STRING;
					mii.dwTypeData = tmp;
					mii.cch = strlen(tmp);
					mii.wID = 3;

					InsertMenuItem(submenu, 0, TRUE, &mii);
				}

				mir_snprintf(tmp, sizeof(tmp), Translate("Set My Nickname for %s..."), proto->description);

				ZeroMemory(&mii, sizeof(mii));
				mii.cbSize = sizeof(mii);
				mii.fMask = MIIM_ID | MIIM_TYPE;
				mii.fType = MFT_STRING;
				mii.dwTypeData = tmp;
				mii.cch = strlen(tmp);
				mii.wID = 2;

				if (!proto->CanSetNick())
				{
					mii.fMask |= MIIM_STATE;
					mii.fState = MFS_DISABLED;
				}

				InsertMenuItem(submenu, 0, TRUE, &mii);

				mir_snprintf(tmp, sizeof(tmp), Translate("Set My Avatar for %s..."), proto->description);

				ZeroMemory(&mii, sizeof(mii));
				mii.cbSize = sizeof(mii);
				mii.fMask = MIIM_ID | MIIM_TYPE;
				mii.fType = MFT_STRING;
				mii.dwTypeData = tmp;
				mii.cch = strlen(tmp);
				mii.wID = 1;

				if (!proto->CanSetAvatar())
				{
					mii.fMask |= MIIM_STATE;
					mii.fState = MFS_DISABLED;
				}

				InsertMenuItem(submenu, 0, TRUE, &mii);

				ClientToScreen(hwnd, &p);
	
				int ret = TrackPopupMenu(submenu, TPM_TOPALIGN|TPM_LEFTALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL);
				DestroyMenu(menu);

				switch(ret)
				{
					case 1:
					{
						CallService(MS_MYDETAILS_SETMYAVATARUI, 0, (LPARAM) proto->name);
						break;
					}
					case ID_AVATARPOPUP_SETMYAVATAR:
					{
						CallService(MS_MYDETAILS_SETMYAVATARUI, 0, 0);
						break;
					}
					case 2:
					{
						CallService(MS_MYDETAILS_SETMYNICKNAMEUI, 0, (LPARAM) proto->name);
						break;
					}
					case ID_NICKPOPUP_SETMYNICKNAME:
					{
						CallService(MS_MYDETAILS_SETMYNICKNAMEUI, 0, 0);
						break;
					}
					case 3:
					{
						CallService(MS_MYDETAILS_SETMYSTATUSMESSAGEUI, 0, (LPARAM) proto->name);
						break;
					}
					case ID_STATUSMESSAGEPOPUP_SETMYSTATUSMESSAGE:
					{
						CallService(MS_MYDETAILS_SETMYSTATUSMESSAGEUI, 0, 0);
						break;
					}
					case ID_SHOW_NEXT_PROTO:
					{
						CallService(MS_MYDETAILS_SHOWNEXTPROTOCOL, 0, 0);
						break;
					}
					case ID_SHOW_PREV_PROTO:
					{
						CallService(MS_MYDETAILS_SHOWPREVIOUSPROTOCOL, 0, 0);
						break;
					}
					case ID_CICLE_THORUGHT_PROTOS:
					{
						CallService(MS_MYDETAILS_CICLE_THROUGHT_PROTOCOLS, TRUE, 0);
						break;
					}
					case ID_DONT_CICLE_THORUGHT_PROTOS:
					{
						CallService(MS_MYDETAILS_CICLE_THROUGHT_PROTOCOLS, FALSE, 0);
						break;
					}
				}
			}

			data->showing_menu = false;


			break;
		}

		case WM_MOUSELEAVE:
		{
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_HOVER;
			tme.hwndTrack = hwnd;
			tme.dwHoverTime = HOVER_DEFAULT;
			TrackMouseEvent(&tme);
		}
		case WM_NCMOUSEMOVE:
		{
			MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);

			MakeHover(hwnd, data->draw_img, &data->mouse_over_img, NULL, NULL);
			MakeHover(hwnd, data->draw_nick, &data->mouse_over_nick, NULL, NULL);
			MakeHover(hwnd, data->draw_proto, &data->mouse_over_proto, NULL, NULL);
			MakeHover(hwnd, data->draw_status, &data->mouse_over_status, NULL, NULL);
			MakeHover(hwnd, data->draw_away_msg, &data->mouse_over_away_msg, NULL, NULL);

			break;
		}

		case WM_MOUSEHOVER:
		{
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hwnd;
			tme.dwHoverTime = HOVER_DEFAULT;
			TrackMouseEvent(&tme);
		}
		case WM_MOUSEMOVE:
		{
			MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
			Protocol *proto = protocols->Get(data->protocol_number);

			POINT p;
			p.x = LOWORD(lParam); 
			p.y = HIWORD(lParam); 

			MakeHover(hwnd, data->draw_img, &data->mouse_over_img, &p, &data->img_rect);
			MakeHover(hwnd, data->draw_nick, &data->mouse_over_nick, &p, &data->nick_rect);
			MakeHover(hwnd, data->draw_proto, &data->mouse_over_proto, &p, &data->proto_rect);
			MakeHover(hwnd, data->draw_status, &data->mouse_over_status, &p, &data->status_rect);
			MakeHover(hwnd, data->draw_away_msg, &data->mouse_over_away_msg, &p, &data->away_msg_rect);

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR lpnmhdr = (LPNMHDR) lParam;

			int i = (int) lpnmhdr->code;

			switch (lpnmhdr->code) {
				case TTN_GETDISPINFO:
				{
					MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
					Protocol *proto = protocols->Get(data->protocol_number);

					LPNMTTDISPINFO lpttd = (LPNMTTDISPINFO) lpnmhdr;
					SendMessage(lpnmhdr->hwndFrom, TTM_SETMAXTIPWIDTH, 0, 300);
					
					if (lpnmhdr->hwndFrom == data->nick_tt_hwnd)
						lpttd->lpszText = proto->nickname;
					else if (lpnmhdr->hwndFrom == data->status_tt_hwnd)
						lpttd->lpszText = proto->status_name;
					else if (lpnmhdr->hwndFrom == data->away_msg_tt_hwnd)
						lpttd->lpszText = proto->status_message;

					return 0;
				}
			}

			break;
		}

		case WM_DESTROY:
		{
			KillTimer(hwnd, ID_FRAME_TIMER);

			MyDetailsFrameData *tmp = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
			DeleteTooltipWindows(tmp);
			if (tmp != NULL) delete tmp;

			break;
		}

		// Custom Messages //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		case MWM_REFRESH:
		{
			MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
//			data->recalc_rectangles = true;

			KillTimer(hwnd, ID_RECALC_TIMER);
			SetTimer(hwnd, ID_RECALC_TIMER, RECALC_TIME, NULL);
			break;
		}

		case MWM_AVATAR_CHANGED:
		{
			Protocol *proto = protocols->Get((const char *) wParam);

			if (proto != NULL)
			{
				proto->GetAvatar();
				RefreshFrame();
			}

			break;
		}

		case MWM_NICK_CHANGED:
		{
			Protocol *proto = protocols->Get((const char *) wParam);

			if (proto != NULL)
			{
				proto->GetNick();
				RefreshFrame();
			}

			break;
		}

		case MWM_STATUS_CHANGED:
		{
			Protocol *proto = protocols->Get((const char *) wParam);

			if (proto != NULL)
			{
				proto->GetStatus();
				proto->GetStatusMsg();
				proto->GetNick();

				RefreshFrame();
			}

			break;
		}

		case MWM_STATUS_MSG_CHANGED:
		{
			MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd, GWL_USERDATA);
			data->get_status_messages = true;

			RefreshFrame();
			break;
		}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}


int ShowHideMenuFunc(WPARAM wParam, LPARAM lParam) 
{
	if (MyDetailsFrameVisible())
	{
		SendMessage(hwnd_container, WM_CLOSE, 0, 0);
	}
	else 
	{
		ShowWindow(hwnd_container, SW_SHOW);
		DBWriteContactSettingByte(0, MODULE_NAME, SETTING_FRAME_VISIBLE, 1);
	}

	FixMainMenu();
	return 0;
}


void FixMainMenu() 
{
	CLISTMENUITEM mi = {0};
	mi.cbSize = sizeof(CLISTMENUITEM);
	mi.flags = CMIM_NAME;

	if(MyDetailsFrameVisible())
		mi.pszName = Translate("Hide My Details");
	else
		mi.pszName = Translate("Show My Details");

	CallService(MS_CLIST_MODIFYMENUITEM, (WPARAM)hMenuShowHideFrame, (LPARAM)&mi);
}

#include <math.h>

void RedrawFrame() 
{
//	MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd_frame, GWL_USERDATA);
//	if (data != NULL) 
//	{
//		data->recalc_rectangles = true;

		if(frame_id == -1) 
		{
			InvalidateRect(hwnd_container, NULL, TRUE);
		}
		else
		{
			CallService(MS_CLIST_FRAMES_UPDATEFRAME, (WPARAM)frame_id, (LPARAM)FU_TBREDRAW | FU_FMREDRAW);
		}
//	}
}

void RefreshFrameAndCalcRects() 
{
	if (hwnd_frame != NULL)
	{
		MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd_frame, GWL_USERDATA);
		data->recalc_rectangles = true;

		PostMessage(hwnd_frame, MWM_REFRESH, 0, 0);
	}
}

void RefreshFrame() 
{
	if (hwnd_frame != NULL)
		PostMessage(hwnd_frame, MWM_REFRESH, 0, 0);
}

// only used when no multiwindow functionality is available
bool MyDetailsFrameVisible() 
{
	return IsWindowVisible(hwnd_container) ? true : false;
}

void SetMyDetailsFrameVisible(bool visible) 
{
	if (frame_id == -1 && hwnd_container != 0) 
	{
		ShowWindow(hwnd_container, visible ? SW_SHOW : SW_HIDE);
	}
}

void SetCicleTime()
{
	if (hwnd_frame != NULL)
		SetCicleTime(hwnd_frame);
}

void SetCicleTime(HWND hwnd)
{
	KillTimer(hwnd, ID_FRAME_TIMER);

	if (opts.cicle_throught_protocols)
		SetTimer(hwnd, ID_FRAME_TIMER, opts.seconds_to_show_protocol * 1000, 0);
}

void SetStatusMessageRefreshTime()
{
	if (hwnd_frame != NULL)
		SetStatusMessageRefreshTime(hwnd_frame);
}

void SetStatusMessageRefreshTime(HWND hwnd)
{
	KillTimer(hwnd, ID_STATUSMESSAGE_TIMER);

	opts.refresh_status_message_timer = DBGetContactSettingWord(NULL,"MyDetails","RefreshStatusMessageTimer",12);
	if (opts.refresh_status_message_timer > 0)
	{
		SetTimer(hwnd, ID_STATUSMESSAGE_TIMER, opts.refresh_status_message_timer * 1000, NULL);
	}
}

int PluginCommand_ShowNextProtocol(WPARAM wParam,LPARAM lParam)
{
	if (hwnd_frame == NULL)
		return -1;

	MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd_frame, GWL_USERDATA);

	data->protocol_number ++;
	if (data->protocol_number >= protocols->GetSize())
	{
		data->protocol_number = 0;
	}

	DBWriteContactSettingWord(NULL,"MyDetails","ProtocolNumber",data->protocol_number);

	data->recalc_rectangles = true;

	SetCicleTime();

	RedrawFrame();

	return 0;
}

int PluginCommand_ShowPreviousProtocol(WPARAM wParam,LPARAM lParam)
{
	if (hwnd_frame == NULL)
		return -1;

	MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd_frame, GWL_USERDATA);

	data->protocol_number --;
	if (data->protocol_number < 0)
	{
		data->protocol_number = protocols->GetSize() - 1;
	}

	DBWriteContactSettingWord(NULL,"MyDetails","ProtocolNumber",data->protocol_number);

	data->recalc_rectangles = true;

	SetCicleTime();

	RedrawFrame();

	return 0;
}

int PluginCommand_ShowProtocol(WPARAM wParam,LPARAM lParam)
{
	char * proto = (char *)lParam;
	int proto_num = -1;

	if (proto == NULL)
		return -1;

	for(int i = 0 ; i < protocols->GetSize() ; i++)
	{
		if (stricmp(protocols->Get(i)->name, proto) == 0)
		{
			proto_num = i;
			break;
		}
	}

	if (proto_num == -1)
		return -2;

	if (hwnd_frame == NULL)
		return -3;

	MyDetailsFrameData *data = (MyDetailsFrameData *)GetWindowLong(hwnd_frame, GWL_USERDATA);

	data->protocol_number = proto_num;
	DBWriteContactSettingWord(NULL,"MyDetails","ProtocolNumber",data->protocol_number);

	data->recalc_rectangles = true;

	SetCicleTime();

	RedrawFrame();

	return 0;
}

int SettingsChangedHook(WPARAM wParam, LPARAM lParam) 
{
	DBCONTACTWRITESETTING *cws = (DBCONTACTWRITESETTING*)lParam;

	if ((HANDLE)wParam == NULL)
	{
		Protocol *proto = protocols->Get((const char *) cws->szModule);

		if (!strcmp(cws->szSetting,"Status") 
				|| ( proto != NULL && proto->custom_status != 0 
					 && proto->custom_status_name != NULL 
					 && !strcmp(cws->szSetting, proto->custom_status_name) )
				|| ( proto != NULL && proto->custom_status != 0 
					 && proto->custom_status_message != NULL 
					 && !strcmp(cws->szSetting, proto->custom_status_message) ))
		{
			// Status changed
			if (hwnd_frame != NULL)
			{
				if (proto != NULL)
					PostMessage(hwnd_frame, MWM_STATUS_CHANGED, (WPARAM) proto->name, 0);
			}
		}
		else if(!strcmp(cws->szSetting,"MyHandle")
				|| !strcmp(cws->szSetting,"UIN") 
				|| !strcmp(cws->szSetting,"Nick") 
				|| !strcmp(cws->szSetting,"FirstName") 
				|| !strcmp(cws->szSetting,"e-mail") 
				|| !strcmp(cws->szSetting,"LastName") 
				|| !strcmp(cws->szSetting,"JID"))
		{
			// Name changed
			if (hwnd_frame != NULL)
			{
				if (proto != NULL)
					PostMessage(hwnd_frame, MWM_NICK_CHANGED, (WPARAM) proto->name, 0);
			}
		}
		else if (strstr(cws->szModule,"Away"))
		{
			// Status message changed
			if (hwnd_frame != NULL)
				PostMessage(hwnd_frame, MWM_STATUS_MSG_CHANGED, 0, 0);
		}
	}

	return 0;
}

int AvatarChangedHook(WPARAM wParam, LPARAM lParam) 
{
	if (hwnd_frame != NULL)
	{
		Protocol *proto = protocols->Get((const char *) wParam);

		if (proto != NULL)
			PostMessage(hwnd_frame, MWM_AVATAR_CHANGED, (WPARAM) proto->name, 0);
	}

	return 0;
}

int ProtoAckHook(WPARAM wParam, LPARAM lParam)
{
	ACKDATA *ack = (ACKDATA*)lParam;

	if (ack->type == ACKTYPE_STATUS) 
	{
		if (hwnd_frame != NULL)
		{
			Protocol *proto = protocols->Get((const char *) ack->szModule);

			if (proto != NULL)
				PostMessage(hwnd_frame, MWM_STATUS_CHANGED, (WPARAM) proto->name, 0);
		}
	}
	else if (ack->type == ACKTYPE_AWAYMSG)
	{
		if (hwnd_frame != NULL)
		{
			Protocol *proto = protocols->Get((const char *) ack->szModule);

			if (proto != NULL)
				PostMessage(hwnd_frame, MWM_STATUS_MSG_CHANGED, (WPARAM) proto->name, 0);
		}
	}

	return 0;
}
