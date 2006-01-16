/*
 * YAMN plugin main file
 * Miranda homepage: http://miranda-icq.sourceforge.net/
 * YAMN homepage: http://www.majvan.host.sk/Projekty/YAMN
 *
 * initializes all variables for further work
 *
 * (c) majvan 2002-2004
 */


#include "main.h"
#include "yamn.h"
#include "resources/resource.h"

//- imported ---------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

//CRITICAL_SECTION MWCS;
//CRITICAL_SECTION ASCS;
//CRITICAL_SECTION PRCS;

extern LPCRITICAL_SECTION PluginRegCS;
extern HANDLE ExitEV;
extern HANDLE WriteToFileEV;

extern int PosX,PosY,SizeX,SizeY;

//From account.cpp
extern LPCRITICAL_SECTION AccountStatusCS;
extern LPCRITICAL_SECTION FileWritingCS;
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

WCHAR *ProfileName;		//e.g. "majvan"
WCHAR *UserDirectory;		//e.g. "F:\WINNT\Profiles\UserXYZ"
char *ProtoName;
char *AltProtoName;
char *szMirandaDir;

int YAMN_STATUS;

BOOL UninstallPlugins;

HINSTANCE *hDllPlugins;
static int iDllPlugins=0;

PLUGINLINK *pluginLink;
YAMN_VARIABLES YAMNVar;

PLUGININFO pluginInfo={
	sizeof(PLUGININFO),
	YAMN_SHORTNAME,
	YAMN_VERSION,
	"Mail notifier and browser for Miranda IM. Included POP3 protocol.",
	"tweety (majvan)",
	"francois.mean@skynet.be",
	"� (2002-2004 majvan) 2005 tweety",
	"http://www.miranda-im.org/download/details.php?action=viewfile&id=2165", //"http://www.majvan.host.sk/Projekty/YAMN?fm=soft",
	0,		//not transient
	0		//doesn't replace anything built-in
};

SKINSOUNDDESC NewMailSound={
	sizeof(SKINSOUNDDESC),
	YAMN_NEWMAILSOUND,	//name to refer to sound when playing and in db
	YAMN_NEWMAILSNDDESC,	//description for options dialog
	"",	   		//default sound file to use, without path
};

SKINSOUNDDESC ConnectFailureSound={
	sizeof(SKINSOUNDDESC),
	YAMN_CONNECTFAILSOUND,	//name to refer to sound when playing and in db
	YAMN_CONNECTFAILSNDDESC,//description for options dialog
	"",	   		//default sound file to use, without path
};

HICON hYamnIcon;
HICON hNeutralIcon;
HICON hNewMailIcon;
HICON hConnectFailIcon;
HICON hTopToolBarUp;
HICON hTopToolBarDown;

HANDLE hNewMailHook;
//HANDLE hUninstallPluginsHook;

HANDLE NoWriterEV;

HANDLE hTTButton;		//TopToolBar button

DWORD HotKeyThreadID;

UINT SecTimer;

//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

static void GetProfileDirectory(char *szPath,int cbPath)
//This is copied from Miranda's sources. In 0.2.1.0 it is needed, in newer vesions of Miranda use MS_DB_GETPROFILEPATH service
{
	char *str2;
	char szMirandaIni[MAX_PATH],szProfileDir[MAX_PATH],szExpandedProfileDir[MAX_PATH];
	DWORD dwAttributes;

	szMirandaDir=new char[MAX_PATH];
	GetModuleFileName(GetModuleHandle(NULL),szMirandaDir,MAX_PATH);
	str2=strrchr(szMirandaDir,'\\');
	if(str2!=NULL) *str2=0;
	lstrcpy(szMirandaIni,szMirandaDir);
	lstrcat(szMirandaIni,"\\mirandaboot.ini");
	GetPrivateProfileString("Database","ProfileDir",".",szProfileDir,sizeof(szProfileDir),szMirandaIni);
	ExpandEnvironmentStrings(szProfileDir,szExpandedProfileDir,sizeof(szExpandedProfileDir));
	_chdir(szMirandaDir);
	if(!_fullpath(szPath,szExpandedProfileDir,cbPath))
		lstrcpyn(szPath,szMirandaDir,cbPath);
	if(szPath[lstrlen(szPath)-1]=='\\') szPath[lstrlen(szPath)-1]='\0';
	if((dwAttributes=GetFileAttributes(szPath))!=0xffffffff&&dwAttributes&FILE_ATTRIBUTE_DIRECTORY) return;
	CreateDirectory(szPath,NULL);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved)
{
	char szProfileDir[MAX_PATH+1];
	OSVERSIONINFO OSversion;
	
	OSversion.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);

	GetVersionEx(&OSversion);
	switch(OSversion.dwPlatformId)
	{
		case VER_PLATFORM_WIN32s:
		case VER_PLATFORM_WIN32_WINDOWS:
#ifndef WIN9X
			MessageBoxA(NULL,"This YAMN cannot run on Windows 95, 98 or Me. Why? Read FAQ. You should download Win9x version.","YAMN error",MB_OK | MB_ICONSTOP);
			return FALSE;
#else
			break;
#endif
		case VER_PLATFORM_WIN32_NT:
#ifdef WIN9X
			MessageBoxA(NULL,"This YAMN is intended for Windows 95, 98 or Me. You should use native WinNT version.","YAMN error",MB_OK | MB_ICONSTOP);
			return FALSE;
#else
			break;
#endif
	}

	YAMNVar.hInst=hinstDLL;
	if(fdwReason==DLL_PROCESS_ATTACH)
	{
		if(NULL==(UserDirectory=new WCHAR[MAX_PATH]))
			return FALSE;
		GetProfileDirectory(szProfileDir,sizeof(szProfileDir));
		MultiByteToWideChar(CP_ACP,MB_USEGLYPHCHARS,szProfileDir,-1,UserDirectory,strlen(szProfileDir)+1);
//	we get the user path where our yamn-account.book.ini is stored from mirandaboot.ini file
	}

	return TRUE;
}

extern "C" __declspec(dllexport) PLUGININFO* MirandaPluginInfo(DWORD mirandaVersion)
{
	return &pluginInfo;
}

extern "C" int __declspec(dllexport) Load(PLUGINLINK *link)
{
	
	UINT mod,vk;
	char pn[MAX_PATH+1];
	char *fc;

	pluginLink=link;

	ProtoName = "YAMN";
	YAMN_STATUS = ID_STATUS_ONLINE;

	if(ServiceExists(MS_SKIN2_ADDICON))
	{
		//Icon to show in contact list
		DBVARIANT dbv;
		if(!DBGetContactSetting(NULL,"SkinIcons","YAMN_Neutral",&dbv)) 
		{
			DBWriteContactSettingString(NULL, "Icons", "YAMN40072", (char *)dbv.pszVal);			
			DBFreeVariant(&dbv);
		}
		else
			DBWriteContactSettingString(NULL,"Icons", "YAMN40072", "plugins\\YAMN.dll,-119");
	}
	else
	{
		DBWriteContactSettingString(NULL, "Icons", "YAMN40072", "plugins\\YAMN.dll,-119");
	}

	//Registering YAMN as protocol
	PROTOCOLDESCRIPTOR pd;

	memset(&pd,0,sizeof(pd));
	pd.cbSize=sizeof(pd);
	pd.szName=ProtoName;
	pd.type=PROTOTYPE_PROTOCOL;
	
	CallService(MS_PROTO_REGISTERMODULE,0,(LPARAM)&pd);

	if(NULL==(ProfileName=new WCHAR[MAX_PATH]))
		return 1;

	CallService(MS_DB_GETPROFILENAME,(WPARAM)sizeof(pn),(LPARAM)&(*pn));	//not to pass entire array to fcn
	if(NULL!=(fc=strrchr(pn,(int)'.')))
		*fc=0;
	MultiByteToWideChar(CP_ACP,MB_USEGLYPHCHARS,pn,-1,ProfileName,strlen(pn)+1);

	if(NULL==(AccountStatusCS=new CRITICAL_SECTION))
		return 1;
	if(NULL==(FileWritingCS=new CRITICAL_SECTION))
		return 1;
	if(NULL==(PluginRegCS=new CRITICAL_SECTION))
		return 1;

	InitializeCriticalSection(AccountStatusCS);
	InitializeCriticalSection(FileWritingCS);
	InitializeCriticalSection(PluginRegCS);

	if(NULL==(NoWriterEV=CreateEvent(NULL,TRUE,TRUE,NULL)))
		return 1;
	if(NULL==(WriteToFileEV=CreateEvent(NULL,FALSE,FALSE,NULL)))
		return 1;
	if(NULL==(ExitEV=CreateEvent(NULL,TRUE,FALSE,NULL)))
		return 1;
//	AccountWriterSO=new SCOUNTER(NoWriterEV);

	NewMailSound.pszDescription=Translate(YAMN_NEWMAILSNDDESC);
	ConnectFailureSound.pszDescription=Translate(YAMN_CONNECTFAILSNDDESC);

	PosX=DBGetContactSettingDword(NULL,YAMN_DBMODULE,YAMN_DBPOSX,0);
	PosY=DBGetContactSettingDword(NULL,YAMN_DBMODULE,YAMN_DBPOSY,0);
	SizeX=DBGetContactSettingDword(NULL,YAMN_DBMODULE,YAMN_DBSIZEX,800);
	SizeY=DBGetContactSettingDword(NULL,YAMN_DBMODULE,YAMN_DBSIZEY,200);

//Create new window queues for broadcast messages
	YAMNVar.MessageWnds=(HANDLE)CallService(MS_UTILS_ALLOCWINDOWLIST,0,0);
	YAMNVar.NewMailAccountWnd=(HANDLE)CallService(MS_UTILS_ALLOCWINDOWLIST,0,0);
	YAMNVar.Shutdown=FALSE;

#ifdef YAMN_DEBUG
	InitDebug();
#endif


	CreateServiceFunctions();

	CallService(MS_SKIN_ADDNEWSOUND,0,(LPARAM)&NewMailSound);
	CallService(MS_SKIN_ADDNEWSOUND,0,(LPARAM)&ConnectFailureSound);

	hNewMailHook=CreateHookableEvent(ME_YAMN_NEWMAIL);
//	hUninstallPluginsHook=CreateHookableEvent(ME_YAMN_UNINSTALLPLUGINS);

	HookEvents();

	LoadPlugins();

	WordToModAndVk(DBGetContactSettingWord(NULL,YAMN_DBMODULE,YAMN_HKCHECKMAIL,YAMN_DEFAULTHK),&mod,&vk);
		
//Create thread for hotkey
	CloseHandle(CreateThread(NULL,0,YAMNHotKeyThread,(LPVOID)MAKEWORD((BYTE)vk,(BYTE)mod),0,&HotKeyThreadID));
//Create thread that will be executed every second
	if(!(SecTimer=SetTimer(NULL,0,1000,(TIMERPROC)TimerProc)))
		return 1;


#ifdef YAMN_VER_BETA
	#ifndef YAMN_DEBUG
	#error Configure this YAMN to be debug version
	#endif

	#ifdef NDEBUG
	#error Please force compiler to include debug information
	#endif

	#ifdef YAMN_VER_BETA_CRASHONLY
	MessageBox(NULL,"This YAMN beta version is intended for testing. After crash, you should send report to author. Please read included readme when available. Thank you.","YAMN beta",MB_OK);
	#else
	MessageBox(NULL,"This YAMN beta version is intended for testing. You should inform author if it works or when it does not work. Please read included readme when available. Thank you.","YAMN beta",MB_OK);
	#endif
#endif
	
	return 0;
}

extern "C" int __declspec(dllexport) UninstallEx(PLUGINUNINSTALLPARAMS* ppup)
{
	const char* DocFiles[]={"YAMN-License.txt","YAMN-Readme.txt","YAMN-Readme.developers.txt",NULL};

	typedef int (* UNINSTALLFILTERFCN)();
	UNINSTALLFILTERFCN UninstallFilter;

	PUIRemoveSkinSound(YAMN_NEWMAILSOUND);
	PUIRemoveSkinSound(YAMN_CONNECTFAILSOUND);

	if(UninstallPlugins)
	{
		for(int i=0;i<iDllPlugins;i++)
		{
			if(NULL!=(UninstallFilter=(UNINSTALLFILTERFCN)GetProcAddress(hDllPlugins[i],"UninstallFilter")))
				UninstallFilter();

			FreeLibrary(hDllPlugins[i]);
			hDllPlugins[i]=NULL;				//for safety
		}
//		NotifyEventHooks(ME_YAMN_UNINSTALLPLUGINS,0,0);
	}
	UninstallPOP3(ppup);

	MessageBoxA(NULL,"You have to delete manually YAMN plugins located in \"Plugins/YAMN\" folder.","YAMN uninstalling",MB_OK|MB_ICONINFORMATION);
	PUIRemoveFilesInDirectory(ppup->pszDocsPath,DocFiles);
	if(ppup->bDoDeleteSettings)
		PUIRemoveDbModule("YAMN");
	return 0;
}

int Shutdown(WPARAM,LPARAM)
{
	YAMNVar.Shutdown=TRUE;
//	CallService(MS_TTB_REMOVEBUTTON,(WPARAM)hTTButton,0);		//this often leads to deadlock in Miranda (bug in Miranda)
	KillTimer(NULL,SecTimer);

	UnregisterProtoPlugins();
	UnregisterFilterPlugins();
	return 0;
}

//We undo all things from Load()
extern "C" int __declspec(dllexport) Unload(void)
{
#ifdef YAMN_DEBUG
	UnInitDebug();
#endif
	CloseHandle(NoWriterEV);
	CloseHandle(WriteToFileEV);
	CloseHandle(ExitEV);

	DeleteCriticalSection(AccountStatusCS);
	DeleteCriticalSection(FileWritingCS);
	DeleteCriticalSection(PluginRegCS);

	delete AccountStatusCS;
	delete PluginRegCS;

	return 0;
}

void LoadPlugins()
{
	HANDLE hFind;
	WIN32_FIND_DATA fd;
	char szSearchPath[MAX_PATH];
	char szPluginPath[MAX_PATH];
	lstrcpy(szSearchPath,szMirandaDir);
	lstrcat(szSearchPath,"\\Plugins\\YAMN\\*.dll");
	typedef int (*LOADFILTERFCN)(MIRANDASERVICE GetYAMNFcn);

	hDllPlugins=NULL;

	if(INVALID_HANDLE_VALUE!=(hFind=FindFirstFile(szSearchPath,&fd)))
	{
		do
		{	//rewritten from Miranda sources... Needed because Win32 API has a bug in FindFirstFile, search is done for *.dlllllll... too
			char *dot=strrchr(fd.cFileName,'.');
			if(dot)
			{ // we have a dot
				int len=strlen(fd.cFileName); // find the length of the string
				char* end=fd.cFileName+len; // get a pointer to the NULL
				int safe=(end-dot)-1;	// figure out how many chars after the dot are "safe", not including NULL
		
				if((safe!=3) || (lstrcmpi(dot+1,"dll")!=0)) //not bound, however the "dll" string should mean only 3 chars are compared
					continue;
			}
			else
				continue;
			
			HINSTANCE hDll;
			LOADFILTERFCN LoadFilter;

			lstrcpy(szPluginPath,szMirandaDir);
			lstrcat(szPluginPath,"\\Plugins\\YAMN\\");
			lstrcat(szPluginPath,fd.cFileName);
			if((hDll=LoadLibrary(szPluginPath))==NULL) continue;
			LoadFilter=(LOADFILTERFCN)GetProcAddress(hDll,"LoadFilter");
			if(NULL==LoadFilter)
			{
				FreeLibrary(hDll);
				hDll=NULL;
				continue;
			}
			if(!(*LoadFilter)(GetFcnPtrSvc))
			{
				FreeLibrary(hDll);
				hDll=NULL;
			}

			if(hDll!=NULL)
			{
				hDllPlugins=(HINSTANCE *)realloc((void *)hDllPlugins,(iDllPlugins+1)*sizeof(HINSTANCE));
				hDllPlugins[iDllPlugins++]=hDll;
			}
		} while(FindNextFile(hFind,&fd));
	FindClose(hFind);
	}
}

void GetIconSize(HICON hIcon, int* sizeX, int* sizeY)
{
    ICONINFO ii;
    BITMAP bm;
    GetIconInfo(hIcon, &ii);
    GetObject(ii.hbmColor, sizeof(bm), &bm);
    if (sizeX != NULL) *sizeX = bm.bmWidth;
    if (sizeY != NULL) *sizeY = bm.bmHeight;
    DeleteObject(ii.hbmMask);
    DeleteObject(ii.hbmColor);
}

HBITMAP LoadBmpFromIcon(HICON hIcon)
{
	HBITMAP hBmp, hoBmp;
    HDC hdc, hdcMem;
    HBRUSH hBkgBrush;

	int IconSizeX = 16;
    int IconSizeY = 16;

	//GetIconSize(hIcon, &IconSizeX, &IconSizeY);

	//DebugLog(SynchroFile,"Icon size %i %i\n",IconSizeX,IconSizeY);

    if ((IconSizeX == 0) || (IconSizeY == 0)) 
	{    
        IconSizeX = 16;
        IconSizeY = 16;
    }

    RECT rc;
    BITMAPINFOHEADER bih = {0};
    int widthBytes;

    hBkgBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
    bih.biSize = sizeof(bih);
    bih.biBitCount = 24;
    bih.biPlanes = 1;
    bih.biCompression = BI_RGB;
    bih.biHeight = IconSizeY;
    bih.biWidth = IconSizeX; 
    widthBytes = ((bih.biWidth*bih.biBitCount + 31) >> 5) * 4;
    rc.top = rc.left = 0;
    rc.right = bih.biWidth;
    rc.bottom = bih.biHeight;
    hdc = GetDC(NULL);
    hBmp = CreateCompatibleBitmap(hdc, bih.biWidth, bih.biHeight);
    hdcMem = CreateCompatibleDC(hdc);
    hoBmp = (HBITMAP)SelectObject(hdcMem, hBmp);
    FillRect(hdcMem, &rc, hBkgBrush);
    DrawIconEx(hdcMem, 0, 0, hIcon, bih.biWidth, bih.biHeight, 0, NULL, DI_NORMAL);
    SelectObject(hdcMem, hoBmp);

	return hBmp;
}

int AddTopToolbarIcon(WPARAM,LPARAM)
{
	TTBButton Button=
	{
		sizeof(TTBButton),
		NULL,
		NULL,
		NULL,
		MS_YAMN_FORCECHECK,
		TTBBF_VISIBLE | TTBBF_SHOWTOOLTIP, // | TTBBF_DRAWBORDER,
		0,0,0,0,
		NULL
	};

	if(!DBGetContactSettingByte(NULL,YAMN_DBMODULE,YAMN_TTBFCHECK,1))
		return 1;
	
	Button.name=Translate("Check mail");
	
	Button.hbBitmapUp = LoadBmpFromIcon(hTopToolBarUp);
	Button.hbBitmapDown = LoadBmpFromIcon(hTopToolBarDown); //LoadBitmap(YAMNVar.hInst,MAKEINTRESOURCE(IDB_BMTTB));
	
	if((HANDLE)-1==(hTTButton=(HANDLE)CallService(MS_TTB_ADDBUTTON,(WPARAM)&Button,(LPARAM)0)))
		return 1;
	CallService(MS_TTB_SETBUTTONOPTIONS,MAKEWPARAM((WORD)TTBO_TIPNAME,(WORD)hTTButton),(LPARAM)Translate("Check mail"));
	return 0;
}

int UninstallQuestionSvc(WPARAM wParam,LPARAM)
{
//	if(strcmp((char *)wParam,Translate("Yet Another Mail Notifier")))
//		return 0;
	switch(MessageBoxA(NULL,Translate("Do you also want to remove native YAMN plugins settings?"),Translate("YAMN uninstalling"),MB_YESNOCANCEL|MB_ICONQUESTION))
	{
		case IDYES:
			UninstallPlugins=TRUE;
			break;
		case IDNO:
			UninstallPlugins=FALSE;
			break;
		case IDCANCEL:
			return 1;
	}
	return 0;
}