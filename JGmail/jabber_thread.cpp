/*

Jabber Protocol Plugin for Miranda IM
Copyright ( C ) 2002-04  Santithorn Bunchua
Copyright ( C ) 2005-07  George Hazan

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or ( at your option ) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

File name      : $URL$
Revision       : $Revision$
Last change on : $Date$
Last change by : $Author$

*/

#include "jabber.h"

#include <io.h>
#include <windns.h>   // requires Windows Platform SDK

#include "jabber_ssl.h"
#include "jabber_list.h"
#include "jabber_iq.h"
#include "jabber_secur.h"
#include "resource.h"
#include "version.h"

// <iq/> identification number for various actions
// for JABBER_REGISTER thread
int iqIdRegGetReg;
int iqIdRegSetReg;

static void __cdecl JabberKeepAliveThread( JABBER_SOCKET s );
static void JabberProcessStreamOpening( XmlNode *node, void *userdata );
static void JabberProcessStreamClosing( XmlNode *node, void *userdata );
static void JabberProcessProtocol( XmlNode *node, void *userdata );
static void JabberProcessMessage( XmlNode *node, void *userdata );
static void JabberProcessPresence( XmlNode *node, void *userdata );
static void JabberProcessIq( XmlNode *node, void *userdata );
static void JabberProcessProceed( XmlNode *node, void *userdata );
static void JabberProcessRegIq( XmlNode *node, void *userdata );
void JabberUserConfigResult( XmlNode *iqNode, void *userdata );

BOOL IsListeningToStatusMessage(TCHAR *p);
TCHAR *GetListeningToText(TCHAR *p);


void JabberMenuHideSrmmIcon(HANDLE hContact);
void JabberMenuUpdateSrmmIcon(JABBER_LIST_ITEM *item);

extern int bSecureIM;
static VOID CALLBACK JabberDummyApcFunc( DWORD param )
{
	return;
}

static char onlinePassword[128];
static HANDLE hEventPasswdDlg;

static BOOL CALLBACK JabberPasswordDlgProc( HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam )
{
	switch ( msg ) {
	case WM_INITDIALOG:
		TranslateDialogDefault( hwndDlg );
		{	TCHAR text[128];
			mir_sntprintf( text, SIZEOF(text), _T("%s %s"), TranslateT( "Enter password for" ), ( TCHAR* )lParam );
			SetDlgItemText( hwndDlg, IDC_JID, text );
		}
		return TRUE;
	case WM_COMMAND:
		switch ( LOWORD( wParam )) {
		case IDOK:
			GetDlgItemTextA( hwndDlg, IDC_PASSWORD, onlinePassword, SIZEOF( onlinePassword ));
			//EndDialog( hwndDlg, ( int ) onlinePassword );
			//return TRUE;
			// Fall through
		case IDCANCEL:
			//EndDialog( hwndDlg, 0 );
			SetEvent( hEventPasswdDlg );
			DestroyWindow( hwndDlg );
			return TRUE;
		}
		break;
	}

	return FALSE;
}

static VOID CALLBACK JabberPasswordCreateDialogApcProc( DWORD param )
{
	CreateDialogParam( hInst, MAKEINTRESOURCE( IDD_PASSWORD ), NULL, JabberPasswordDlgProc, ( LPARAM )param );
}

static VOID CALLBACK JabberOfflineChatWindows( DWORD )
{
	GCDEST gcd = { jabberProtoName, NULL, GC_EVENT_CONTROL };
	GCEVENT gce = { 0 };
	gce.cbSize = sizeof(GCEVENT);
	gce.pDest = &gcd;
	CallService( MS_GC_EVENT, SESSION_TERMINATE, (LPARAM)&gce );
}

/////////////////////////////////////////////////////////////////////////////////////////
// Jabber keep-alive thread

static void __cdecl JabberKeepAliveThread( JABBER_SOCKET s )
{
	NETLIBSELECT nls = {0};
	nls.cbSize = sizeof( NETLIBSELECT );
	nls.dwTimeout = 60000;	// 60000 millisecond ( 1 minute )
	nls.hExceptConns[0] = s;
	for ( ;; ) {
		if ( JCallService( MS_NETLIB_SELECT, 0, ( LPARAM )&nls ) != 0 )
			break;
		if ( jabberSendKeepAlive )
			JabberSend( s, " \t " );
	}
	JabberLog( "Exiting KeepAliveThread" );
}

/////////////////////////////////////////////////////////////////////////////////////////

typedef DNS_STATUS (WINAPI *DNSQUERYA)(IN PCSTR pszName, IN WORD wType, IN DWORD Options, IN PIP4_ARRAY aipServers OPTIONAL, IN OUT PDNS_RECORD *ppQueryResults OPTIONAL, IN OUT PVOID *pReserved OPTIONAL);
typedef void (WINAPI *DNSFREELIST)(IN OUT PDNS_RECORD pRecordList, IN DNS_FREE_TYPE FreeType);
#ifndef DNS_TYPE_SRV
# define DNS_TYPE_SRV (33)
#endif

static int xmpp_client_query( char* domain )
{
	HINSTANCE hDnsapi = LoadLibraryA( "dnsapi.dll" );
	if ( hDnsapi == NULL )
		return 0;

	DNSQUERYA pDnsQuery = (DNSQUERYA)GetProcAddress(hDnsapi, "DnsQuery_A");
	DNSFREELIST pDnsRecordListFree = (DNSFREELIST)GetProcAddress(hDnsapi, "DnsRecordListFree");
	if ( pDnsQuery == NULL ) {
		//dnsapi.dll is not the needed dnsapi ;)
		FreeLibrary( hDnsapi );
		return 0;
	}

   char temp[256];
	mir_snprintf( temp, SIZEOF(temp), "_xmpp-client._tcp.%s", domain );

	DNS_RECORD *results = NULL;
	DNS_STATUS status = pDnsQuery(temp, DNS_TYPE_SRV, DNS_QUERY_STANDARD, NULL, &results, NULL);
	if (FAILED(status)||!results || results[0].Data.Srv.pNameTarget == 0||results[0].wType != DNS_TYPE_SRV) {
		FreeLibrary(hDnsapi);
		return NULL;
	}

	strncpy(domain, (char*)results[0].Data.Srv.pNameTarget, 127);
	int port = results[0].Data.Srv.wPort;
	pDnsRecordListFree(results, DnsFreeRecordList);
	FreeLibrary(hDnsapi);
	return port;
}

static XmlState xmlState;
static char *xmlStreamToBeInitialized = 0;
static void xmlStreamInitialize(char *which) {
	JabberLog("Stream will be initialized %s",which);
	xmlStreamToBeInitialized = strdup(which);
}
static void xmlStreamInitializeNow(ThreadData* info) {
	JabberLog("Stream is initializing %s",xmlStreamToBeInitialized?xmlStreamToBeInitialized:"after connect");
	if (xmlStreamToBeInitialized) {
		//free(xmlStreamToBeInitialized);
		xmlStreamToBeInitialized = NULL;
		JabberXmlDestroyState(&xmlState);
	}
	JabberXmlInitState( &xmlState );
	JabberXmlSetCallback( &xmlState, 1, ELEM_OPEN, JabberProcessStreamOpening, info );
	JabberXmlSetCallback( &xmlState, 1, ELEM_CLOSE, JabberProcessStreamClosing, info );
	JabberXmlSetCallback( &xmlState, 2, ELEM_CLOSE, JabberProcessProtocol, info );
	{	
		XmlNode stream( "stream:stream" );
		stream.props = "<?xml version='1.0' encoding='UTF-8'?>";
		stream.addAttr( "to", info->server );
		stream.addAttr( "xmlns", "jabber:client" );
		stream.addAttr( "xmlns:stream", "http://etherx.jabber.org/streams" );
		stream.addAttr( "xml:lang", "en" );
		if ( !JGetByte( "Disable3920auth", 0 ))
			stream.addAttr( "version", "1.0" );
		stream.dirtyHack = true; // this is to keep the node open - do not send </stream:stream>
		JabberSend( info->s, stream );
}	}

void __cdecl JabberServerThread( ThreadData* info )
{
	DBVARIANT dbv;
	char* buffer;
	int datalen;
	int oldStatus;
	PVOID ssl;

	JabberLog( "Thread started: type=%d", info->type );

	info->resolveID = -1;
	info->auth = NULL;
	if ( info->type == JABBER_SESSION_NORMAL ) {

		// Normal server connection, we will fetch all connection parameters
		// e.g. username, password, etc. from the database.

		if ( jabberThreadInfo != NULL ) {
			// Will not start another connection thread if a thread is already running.
			// Make APC call to the main thread. This will immediately wake the thread up
			// in case it is asleep in the reconnect loop so that it will immediately
			// reconnect.
			QueueUserAPC( JabberDummyApcFunc, jabberThreadInfo->hThread, 0 );
			JabberLog( "Thread ended, another normal thread is running" );
LBL_Exit:
			delete info;
			return;
		}

		jabberThreadInfo = info;
		if ( streamId ) mir_free( streamId );
		streamId = NULL;

		if ( !JGetStringT( NULL, "LoginName", &dbv )) {
			_tcsncpy( info->username, dbv.ptszVal, SIZEOF( info->username )-1 );
			JFreeVariant( &dbv );
		}

		if ( *rtrim(info->username) == '\0' ) {
			DWORD dwSize = SIZEOF( info->username );
			if ( GetUserName( info->username, &dwSize ))
				JSetStringT( NULL, "LoginName", info->username );
			else
				info->username[0] = 0;
		}

		if ( *rtrim(info->username) == '\0' ) {
			JabberLog( "Thread ended, login name is not configured" );
			JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_BADUSERID );
LBL_FatalError:
			jabberThreadInfo = NULL;
			oldStatus = jabberStatus;
			jabberStatus = ID_STATUS_OFFLINE;
			JSendBroadcast( NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, ( HANDLE ) oldStatus, jabberStatus );
         goto LBL_Exit;
		}

		if ( !DBGetContactSetting( NULL, jabberProtoName, "LoginServer", &dbv )) {
			strncpy( info->server, dbv.pszVal, SIZEOF( info->server )-1 );
			JFreeVariant( &dbv );
		}
		else {
			JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_NONETWORK );
			JabberLog( "Thread ended, login server is not configured" );
			goto LBL_FatalError;
		}

		if ( !JGetStringT( NULL, "Resource", &dbv )) {
			_tcsncpy( info->resource, dbv.ptszVal, SIZEOF( info->resource )-1 );
			JFreeVariant( &dbv );
		}
		else _tcscpy( info->resource, _T("Miranda"));

		TCHAR jidStr[128];
		mir_sntprintf( jidStr, SIZEOF( jidStr ), _T("%s@") _T(TCHAR_STR_PARAM) _T("/%s"), info->username, info->server, info->resource );
		_tcsncpy( info->fullJID, jidStr, SIZEOF( info->fullJID )-1 );

		if ( JGetByte( "SavePassword", TRUE ) == FALSE ) {
			mir_sntprintf( jidStr, SIZEOF( jidStr ), _T("%s@") _T(TCHAR_STR_PARAM), info->username, info->server );

			// Ugly hack: continue logging on only the return value is &( onlinePassword[0] )
			// because if WM_QUIT while dialog box is still visible, p is returned with some
			// exit code which may not be NULL.
			// Should be better with modeless.
			onlinePassword[0] = ( char )-1;
			hEventPasswdDlg = CreateEvent( NULL, FALSE, FALSE, NULL );
			QueueUserAPC( JabberPasswordCreateDialogApcProc, hMainThread, ( DWORD )jidStr );
			WaitForSingleObject( hEventPasswdDlg, INFINITE );
			CloseHandle( hEventPasswdDlg );

			if ( onlinePassword[0] == ( TCHAR ) -1 ) {
				JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_BADUSERID );
				JabberLog( "Thread ended, password request dialog was canceled" );
				goto LBL_FatalError;
			}
			strncpy( info->password, onlinePassword, SIZEOF( info->password ));
			info->password[ SIZEOF( info->password )-1] = '\0';
		}
		else {
			if ( DBGetContactSetting( NULL, jabberProtoName, "Password", &dbv )) {
				JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_BADUSERID );
				JabberLog( "Thread ended, password is not configured" );
				goto LBL_FatalError;
			}
			JCallService( MS_DB_CRYPT_DECODESTRING, strlen( dbv.pszVal )+1, ( LPARAM )dbv.pszVal );
			strncpy( info->password, dbv.pszVal, SIZEOF( info->password ));
			info->password[SIZEOF( info->password )-1] = '\0';
			JFreeVariant( &dbv );
		}

		if ( JGetByte( "ManualConnect", FALSE ) == TRUE ) {
			if ( !DBGetContactSetting( NULL, jabberProtoName, "ManualHost", &dbv )) {
				strncpy( info->manualHost, dbv.pszVal, SIZEOF( info->manualHost ));
				info->manualHost[sizeof( info->manualHost )-1] = '\0';
				JFreeVariant( &dbv );
			}
			info->port = JGetWord( NULL, "ManualPort", JABBER_DEFAULT_PORT );
		}
		else info->port = JGetWord( NULL, "Port", JABBER_DEFAULT_PORT );

		info->useSSL = JGetByte( "UseSSL", FALSE );
	}

	else if ( info->type == JABBER_SESSION_REGISTER ) {
		// Register new user connection, all connection parameters are already filled-in.
		// Multiple thread allowed, although not possible : )
		// thinking again.. multiple thread should not be allowed
		info->reg_done = FALSE;
		SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 25, ( LPARAM )TranslateT( "Connecting..." ));
		iqIdRegGetReg = -1;
		iqIdRegSetReg = -1;
	}
	else {
		JabberLog( "Thread ended, invalid session type" );
		goto LBL_Exit;
	}

	char connectHost[128];
	if ( info->manualHost[0] == 0 ) {
		int port_temp;
		strncpy( connectHost, info->server, SIZEOF(info->server));
		if ( port_temp = xmpp_client_query( connectHost )) { // port_temp will be > 0 if resolution is successful
			JabberLog("%s%s resolved to %s:%d","_xmpp-client._tcp.",info->server,connectHost,port_temp);
			if (info->port==0 || info->port==5222)
				info->port = port_temp;
		}
		else JabberLog("%s%s not resolved", "_xmpp-client._tcp.", connectHost);
	}
	else strncpy( connectHost, info->manualHost, SIZEOF(connectHost)); // do not resolve if manual host is selected

	JabberLog( "Thread type=%d server='%s' port='%d'", info->type, connectHost, info->port );

	int jabberNetworkBufferSize = 2048;
	if (( buffer=( char* )mir_alloc( jabberNetworkBufferSize+1 )) == NULL ) {	// +1 is for '\0' when debug logging this buffer
		JabberLog( "Cannot allocate network buffer, thread ended" );
		if ( info->type == JABBER_SESSION_NORMAL ) {
			oldStatus = jabberStatus;
			jabberStatus = ID_STATUS_OFFLINE;
			JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_NONETWORK );
			JSendBroadcast( NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, ( HANDLE ) oldStatus, jabberStatus );
			jabberThreadInfo = NULL;
		}
		else if ( info->type == JABBER_SESSION_REGISTER ) {
			SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, ( LPARAM )TranslateT( "Error: Not enough memory" ));
		}
		JabberLog( "Thread ended, network buffer cannot be allocated" );
		goto LBL_Exit;
	}

	info->s = JabberWsConnect( connectHost, info->port );
	if ( info->s == NULL ) {
		JabberLog( "Connection failed ( %d )", WSAGetLastError());
		if ( info->type == JABBER_SESSION_NORMAL ) {
			if ( jabberThreadInfo == info ) {
				oldStatus = jabberStatus;
				jabberStatus = ID_STATUS_OFFLINE;
				JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_NONETWORK );
				JSendBroadcast( NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, ( HANDLE ) oldStatus, jabberStatus );
				jabberThreadInfo = NULL;
		}	}
		else if ( info->type == JABBER_SESSION_REGISTER )
			SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, ( LPARAM )TranslateT( "Error: Cannot connect to the server" ));

		JabberLog( "Thread ended, connection failed" );
		mir_free( buffer );
		goto LBL_Exit;
	}

	// Determine local IP
	int socket = JCallService( MS_NETLIB_GETSOCKET, ( WPARAM ) info->s, 0 );
	if ( info->type==JABBER_SESSION_NORMAL && socket!=INVALID_SOCKET ) {
		struct sockaddr_in saddr;
		int len;

		len = sizeof( saddr );
		getsockname( socket, ( struct sockaddr * ) &saddr, &len );
		jabberLocalIP = saddr.sin_addr.S_un.S_addr;
		JabberLog( "Local IP = %s", inet_ntoa( saddr.sin_addr ));
	}

	BOOL sslMode = FALSE;
	/*if ( info->port==443){ //fake ssl session - Client Hello
		if ( JGetByte( "EnableGmail", 1 )&1 ) {
			unsigned int fake[] = {//Long Live Ethereal!
				0x03014680, 0x002D0001, 0x01100000, 0x00038000,
				0xC0000780, 0x02400006, 0x00048000, 0x04000080,
				0x00FFFE00, 0xFE000A00, 0x090000FE, 0x00640000,
				0x00006200, 0x06000003, 0xA60C171F, 0xFC78002F,
				0xB12E5546, 0xEAF13983};
			Netlib_Send( info->s,(char *)fake,sizeof(fake), MSG_NODUMP );
			char *buff = (char *)mir_alloc(0x100);
			int i = Netlib_Recv( info->s,buff,0x100, MSG_NODUMP );
			mir_free(buff);
			if (!i){
				JabberLog( "Thread ended, fake HTTPS session failed" );
				goto LBL_Exit;
			}
			info->useSSL = FALSE; // prevent using SSL in gmail -> port 443. 
		}
	} else */if ( info->useSSL ) {
		JabberLog( "Intializing SSL connection" );
		if (
#ifndef STATICSSL
			hLibSSL!=NULL &&
#endif
			socket!=INVALID_SOCKET ) {
			JabberLog( "SSL using socket = %d", socket );
			if (( ssl=pfn_SSL_new( jabberSslCtx )) != NULL ) {
				JabberLog( "SSL create context ok" );
				if ( pfn_SSL_set_fd( ssl, socket ) > 0 ) {
					JabberLog( "SSL set fd ok" );
					if ( pfn_SSL_connect( ssl ) > 0 ) {
						JabberLog( "SSL negotiation ok" );
						JabberSslAddHandle( info->s, ssl );	// This make all communication on this handle use SSL
						sslMode = TRUE;		// Used in the receive loop below
						JabberLog( "SSL enabled for handle = %d", info->s );
					}
					else {
						JabberLog( "SSL negotiation failed" );
						pfn_SSL_free( ssl );
				}	}
				else {
					JabberLog( "SSL set fd failed" );
					pfn_SSL_free( ssl );
		}	}	}

		if ( !sslMode ) {
			if ( info->type == JABBER_SESSION_NORMAL ) {
				oldStatus = jabberStatus;
				jabberStatus = ID_STATUS_OFFLINE;
				JSendBroadcast( NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, ( HANDLE ) oldStatus, jabberStatus );
				JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_NONETWORK );
				if ( jabberThreadInfo == info )
					jabberThreadInfo = NULL;
			}
			else if ( info->type == JABBER_SESSION_REGISTER ) {
				SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, ( LPARAM )TranslateT( "Error: Cannot connect to the server" ));
			}
			mir_free( buffer );
#ifndef STATICSSL
			if ( !hLibSSL )
#endif
				MessagePopup( NULL, TranslateT( "The connection requires an OpenSSL library, which is not installed." ), TranslateT( "Jabber Connection Error" ), MB_OK|MB_ICONSTOP|MB_SETFOREGROUND );
			JabberLog( "Thread ended, SSL connection failed" );
			goto LBL_Exit;
	}	}

	// User may change status to OFFLINE while we are connecting above
	if ( jabberDesiredStatus!=ID_STATUS_OFFLINE || info->type==JABBER_SESSION_REGISTER ) {

		if ( info->type == JABBER_SESSION_NORMAL ) {
			jabberConnected = TRUE;
			int len = _tcslen( info->username ) + strlen( info->server )+1;
			jabberJID = ( TCHAR* )mir_alloc( sizeof( TCHAR)*( len+1 ));
			mir_sntprintf( jabberJID, len+1, _T("%s@") _T(TCHAR_STR_PARAM), info->username, info->server );
			if ( JGetByte( "KeepAlive", 1 ))
				jabberSendKeepAlive = TRUE;
			else
				jabberSendKeepAlive = FALSE;
			mir_forkthread(( pThreadFunc )JabberKeepAliveThread, info->s );
		}

		xmlStreamInitializeNow( info );

		JabberLog( "Entering main recv loop" );
		datalen = 0;

		for ( ;; ) {
			int recvResult, bytesParsed;

			if ( !sslMode ) if (info->useSSL) {
				ssl = JabberSslHandleToSsl( info->s );
				sslMode = TRUE;
			}

			if ( sslMode )
				recvResult = pfn_SSL_read( ssl, buffer+datalen, jabberNetworkBufferSize-datalen );
			else
				recvResult = JabberWsRecv( info->s, buffer+datalen, jabberNetworkBufferSize-datalen );

			JabberLog( "recvResult = %d", recvResult );
			if ( recvResult <= 0 )
				break;
			datalen += recvResult;

			buffer[datalen] = '\0';
			if ( sslMode && DBGetContactSettingByte( NULL, "Netlib", "DumpRecv", TRUE ) == TRUE ) {
				// Emulate netlib log feature for SSL connection
				char* szLogBuffer = ( char* )mir_alloc( recvResult+128 );
				if ( szLogBuffer != NULL ) {
					strcpy( szLogBuffer, "( SSL ) Data received\n" );
					memcpy( szLogBuffer+strlen( szLogBuffer ), buffer+datalen-recvResult, recvResult+1 /* also copy \0 */ );
					Netlib_Logf( hNetlibUser, "%s", szLogBuffer );	// %s to protect against when fmt tokens are in szLogBuffer causing crash
					mir_free( szLogBuffer );
			}	}

			bytesParsed = JabberXmlParse( &xmlState, buffer );
			JabberLog( "bytesParsed = %d", bytesParsed );
			if ( bytesParsed > 0 ) {
				if ( bytesParsed < datalen )
					memmove( buffer, buffer+bytesParsed, datalen-bytesParsed );
				datalen -= bytesParsed;
			}
			else if ( datalen == jabberNetworkBufferSize ) {
				jabberNetworkBufferSize += 65536;
				JabberLog( "Increasing network buffer size to %d", jabberNetworkBufferSize );
				if (( buffer=( char* )mir_realloc( buffer, jabberNetworkBufferSize+1 )) == NULL ) {
					JabberLog( "Cannot reallocate more network buffer, go offline now" );
					break;
			}	}
			else JabberLog( "Unknown state: bytesParsed=%d, datalen=%d, jabberNetworkBufferSize=%d", bytesParsed, datalen, jabberNetworkBufferSize );

			if (xmlStreamToBeInitialized) xmlStreamInitializeNow(info);
		}

		JabberXmlDestroyState(&xmlState);

		if ( info->type == JABBER_SESSION_NORMAL ) {
			jabberOnline = FALSE;
			jabberConnected = FALSE;
			JabberEnableMenuItems( FALSE );
			if ( hwndJabberChangePassword ) {
				//DestroyWindow( hwndJabberChangePassword );
				// Since this is a different thread, simulate the click on the cancel button instead
				SendMessage( hwndJabberChangePassword, WM_COMMAND, MAKEWORD( IDCANCEL, 0 ), 0 );
			}

			if ( jabberChatDllPresent )
				QueueUserAPC( JabberOfflineChatWindows, hMainThread, 0 );

			JabberListRemoveList( LIST_CHATROOM );
			JabberListRemoveList( LIST_BOOKMARK );
			if ( hwndJabberAgents )
				SendMessage( hwndJabberAgents, WM_JABBER_CHECK_ONLINE, 0, 0 );
			if ( hwndJabberGroupchat )
				SendMessage( hwndJabberGroupchat, WM_JABBER_CHECK_ONLINE, 0, 0 );
			if ( hwndJabberJoinGroupchat )
				SendMessage( hwndJabberJoinGroupchat, WM_JABBER_CHECK_ONLINE, 0, 0 );
			if ( hwndJabberBookmarks )
				SendMessage( hwndJabberBookmarks, WM_JABBER_CHECK_ONLINE, 0, 0 );
			if ( hwndJabberAddBookmark)
				SendMessage( hwndJabberAddBookmark, WM_JABBER_CHECK_ONLINE, 0, 0 );

			// Set status to offline
			oldStatus = jabberStatus;
			jabberStatus = ID_STATUS_OFFLINE;
			JSendBroadcast( NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, ( HANDLE ) oldStatus, jabberStatus );

			// Set all contacts to offline
			HANDLE hContact = ( HANDLE ) JCallService( MS_DB_CONTACT_FINDFIRST, 0, 0 );
			while ( hContact != NULL ) {
				if ( !lstrcmpA(( char* )JCallService( MS_PROTO_GETCONTACTBASEPROTO, ( WPARAM ) hContact, 0 ), jabberProtoName ))
				{
					if ( JGetWord( hContact, "Status", ID_STATUS_OFFLINE ) != ID_STATUS_OFFLINE )
						JSetWord( hContact, "Status", ID_STATUS_OFFLINE );
					JabberMenuHideSrmmIcon(hContact);
				}

				hContact = ( HANDLE ) JCallService( MS_DB_CONTACT_FINDNEXT, ( WPARAM ) hContact, 0 );
			}

			mir_free( jabberJID );
			jabberJID = NULL;
			jabberLoggedInTime = 0;
			JabberListWipe();
			if ( hwndJabberAgents ) {
				SendMessage( hwndJabberAgents, WM_JABBER_AGENT_REFRESH, 0, ( LPARAM )"" );
				SendMessage( hwndJabberAgents, WM_JABBER_TRANSPORT_REFRESH, 0, 0 );
			}
			if ( hwndJabberVcard )
				SendMessage( hwndJabberVcard, WM_JABBER_CHECK_ONLINE, 0, 0 );
		}
		else if ( info->type==JABBER_SESSION_REGISTER && !info->reg_done )
			SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, ( LPARAM )TranslateT( "Error: Connection lost" ));
	}
	else if ( info->type == JABBER_SESSION_NORMAL ) {
		oldStatus = jabberStatus;
		jabberStatus = ID_STATUS_OFFLINE;
		JSendBroadcast( NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, ( HANDLE ) oldStatus, jabberStatus );
	}

	Netlib_CloseHandle( info->s );

	if ( sslMode ) {
		pfn_SSL_free( ssl );
		JabberSslRemoveHandle( info->s );
	}

	JabberLog( "Thread ended: type=%d server='%s'", info->type, info->server );

	if ( info->type==JABBER_SESSION_NORMAL && jabberThreadInfo==info ) {
		if ( streamId ) mir_free( streamId );
		streamId = NULL;
		jabberThreadInfo = NULL;
	}

	mir_free( buffer );
	JabberLog( "Exiting ServerThread" );
	goto LBL_Exit;
}

static void JabberIqProcessSearch( XmlNode *node, void *userdata )
{
}

static void JabberPerformRegistration( ThreadData* info )
{
	iqIdRegGetReg = JabberSerialNext();
	XmlNodeIq iq("get",iqIdRegGetReg,(char*)NULL);
	XmlNode* query = iq.addQuery("jabber:iq:register");
	JabberSend(info->s,iq);
	SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 50, ( LPARAM )TranslateT( "Requesting registration instruction..." ));
}

static void JabberPerformIqAuth( ThreadData* info )
{
	if ( info->type == JABBER_SESSION_NORMAL ) {
		int iqId = JabberSerialNext();
		JabberIqAdd( iqId, IQ_PROC_NONE, JabberIqResultGetAuth );
		XmlNodeIq iq( "get", iqId );
		XmlNode* query = iq.addQuery( "jabber:iq:auth" );
		query->addChild( "username", info->username );
		JabberSend( info->s, iq );
	}
	else if ( info->type == JABBER_SESSION_REGISTER )
		JabberPerformRegistration( info );
}

static void JabberProcessStreamOpening( XmlNode *node, void *userdata )
{
	if ( node->name==NULL || strcmp( node->name, "stream:stream" ))
		return;

	ThreadData* info = ( ThreadData* ) userdata;
	if ( info->type == JABBER_SESSION_NORMAL ) {
		TCHAR* sid = JabberXmlGetAttrValue( node, "id" );
		if ( sid != NULL ) {
			char* pszSid = t2a( sid );
			replaceStr( streamId, pszSid );
			mir_free( pszSid );
	}	}

	if ( JGetByte( "Disable3920auth", 0 ))
		JabberPerformIqAuth( info );
}

static void JabberProcessStreamClosing( XmlNode *node, void *userdata )
{
	ThreadData* info = ( ThreadData* ) userdata;

	Netlib_CloseHandle( info->s );
	if ( node->name && !strcmp( node->name, "stream:error" ) && node->text )
		MessagePopup( NULL, TranslateTS( node->text ), TranslateT( "Jabber Connection Error" ), MB_OK|MB_ICONERROR|MB_SETFOREGROUND );
}

//static int isGoogleTokenUsed = 0; // 1: used; 2: retreived; 3: retreived and used
static void JabberProcessFeatures( XmlNode *node, void *userdata )
{
	ThreadData* info = ( ThreadData* ) userdata;
	bool isPlainAvailable = false;
	bool isMd5available = false;
	bool isNtlmAvailable = false;
	bool isAuthAvailable = false;
	bool isXGoogleTokenAvailable = false;
	bool isRegisterAvailable = false;
	bool areMechanismsDefined = false;
	bool isSessionAvailable = false;
	int i,k;

	for (i = 0; i < node->numChild; i++ ) {
		XmlNode* n = node->child[i];
		if ( !strcmp( n->name, "starttls" )) {
			if ( !info->useSSL &&
				#ifndef STATICSSL
					hLibSSL != NULL &&
				#endif
					JGetByte( "UseTLS", TRUE )) {
				JabberLog( "Requesting TLS" );
				XmlNode stls( n->name ); stls.addAttr( "xmlns", "urn:ietf:params:xml:ns:xmpp-tls" );
				JabberSend( info->s, stls );
				return;
		}	}
		else if ( !strcmp( n->name, "mechanisms" )) {
			areMechanismsDefined = true;
			//JabberLog("%d mechanisms\n",n->numChild);
			for (k = 0; k < n->numChild; k++ ) {
				XmlNode* c = n->child[k];
				if ( !strcmp( c->name, "mechanism" ))
					//JabberLog("Mechanism: %s",c->text);
					     if ( !_tcscmp( c->text, _T("PLAIN")))          isPlainAvailable = true;
					else if ( !_tcscmp( c->text, _T("DIGEST-MD5")))     isMd5available = true;
					else if ( !_tcscmp( c->text, _T("NTLM")))           isNtlmAvailable = true;
					else if ( !_tcscmp( c->text, _T("X-GOOGLE-TOKEN"))) isXGoogleTokenAvailable = true;
		}	}
		else if ( !strcmp( n->name, "register" )) isRegisterAvailable = true;
		else if ( !strcmp( n->name, "auth"     )) isAuthAvailable = true;
		else if ( !strcmp( n->name, "session"  )) isSessionAvailable = true;
	}

	if ( areMechanismsDefined ) {

		TJabberAuth* auth = NULL;

		if ( isNtlmAvailable ) {
			auth = new TNtlmAuth( info );
			if ( !auth->isValid() ) {
				delete auth;
				auth = NULL;
		}	}

		if ( auth == NULL && isMd5available )
			auth = new TMD5Auth( info );

		if ( auth == NULL && isPlainAvailable )
			auth = new TPlainAuth( info );
		
		if ( auth == NULL && isXGoogleTokenAvailable ){
			auth = new TGoogleAuth( info ); // here we request it
			if ( !auth->isValid()){
				//ToDo: here we fall either when the password is wrong 
				//or failed to connect to the GoogleToken server
				delete auth;
				auth = NULL;
		}	}

		if ( auth == NULL ) {
			if ( isAuthAvailable ) { // no known mechanisms but iq_auth is available
				JabberPerformIqAuth( info );
				return;
			}

			MessagePopup( NULL, TranslateT("No known auth mechanisms available. Giving up."), TranslateT( "Jabber Authentication" ), MB_OK|MB_ICONSTOP|MB_SETFOREGROUND );
			JabberSend( info->s, "</stream:stream>" );
			JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_WRONGPASSWORD );
			return;
		}

		if ( info->type == JABBER_SESSION_NORMAL ) {
			info->auth = auth;

			XmlNode n( "auth", auth->getInitialRequest() );
			n.addAttr( "xmlns", _T("urn:ietf:params:xml:ns:xmpp-sasl"));
			n.addAttr( "mechanism", auth->getName() );
			JabberSend( info->s, n );
		}
		else if ( info->type == JABBER_SESSION_REGISTER )
			JabberPerformRegistration( info );
		else
			JabberSend( info->s, "</stream:stream>" );
		return;
	}

	// mechanisms are not defined.
	if ( info->auth ) { //We are already logged-in
		int iqId = JabberSerialNext();
		JabberIqAdd( iqId, IQ_PROC_NONE, JabberIqResultBind );
		XmlNodeIq iq("set",iqId);
		XmlNode* bind = iq.addChild( "bind" ); bind->addAttr( "xmlns", "urn:ietf:params:xml:ns:xmpp-bind" );
		bind->addChild( "resource", info->resource );
		JabberSend( info->s, iq );

		if ( isSessionAvailable )
			info->bIsSessionAvailable = TRUE;

		return;
	}

	//mechanisms not available and we are not logged in
	JabberPerformIqAuth( info );
}

static void __cdecl JabberWaitAndReconnectThread( int unused )
{
	JabberLog("Reconnecting after with new X-GOOGLE-TOKEN");
	Sleep(1000);

	ThreadData* thread = new ThreadData( JABBER_SESSION_NORMAL );
	thread->hThread = ( HANDLE ) mir_forkthread(( pThreadFunc )JabberServerThread, thread );
}

static void JabberProcessFailure( XmlNode *node, void *userdata ) {
//	JabberXmlDumpNode( node );
	ThreadData* info = ( ThreadData* ) userdata;
	TCHAR* type;
//failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"
	if (( type=JabberXmlGetAttrValue( node, "xmlns" )) == NULL ) return;
	if ( !_tcscmp( type, _T("urn:ietf:params:xml:ns:xmpp-sasl") )) {
		JabberSend( info->s, "</stream:stream>" );
		if (!strcmp(info->auth->getName(),"X-GOOGLE-TOKEN") && !info->auth->wasTokenRequested()) { //GoogleToken is used but not requested
			//Old GoogleToken exists, but has expired or invalid
			JDeleteSetting(NULL, "GoogleToken");
			// wait 1 sec and retry
			mir_forkthread( ( pThreadFunc )JabberWaitAndReconnectThread, NULL );
		} else {
			TCHAR text[128];
			mir_sntprintf( text, sizeof( text ), _T("%s %s@")_T(TCHAR_STR_PARAM)_T("."), TranslateT( "Authentication failed for" ), info->username, info->server );
			MessagePopup( NULL, text, TranslateT( "Jabber Authentication" ), MB_OK|MB_ICONSTOP|MB_SETFOREGROUND );
			JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_WRONGPASSWORD );
			jabberThreadInfo = NULL;	// To disallow auto reconnect
		}
	} else JabberLog( "Unknown failure. Ignoring" );
}	

static void JabberProcessError( XmlNode *node, void *userdata )
{
	ThreadData* info = ( ThreadData* ) userdata;
	TCHAR *buff;
	int i;
	int pos;
//failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"
	if ( !node->numChild ) return;
	buff = (TCHAR *)mir_alloc(1024*SIZEOF(buff));
	pos=0;
	for (i=0;i<node->numChild;i++) {
		pos += mir_sntprintf(buff+pos,1024-pos,
			_T(TCHAR_STR_PARAM)_T(": %s\n"),
			node->child[i]->name,node->child[i]->text);
		if (!strcmp(node->child[i]->name,"conflict")) JSendBroadcast( NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_OTHERLOCATION);
	}
	MessagePopup( NULL, buff, TranslateT( "Jabber Error" ), MB_OK|MB_ICONSTOP|MB_SETFOREGROUND );
	mir_free(buff);
	JabberSend( info->s, "</stream:stream>" );
}

static void JabberProcessSuccess( XmlNode *node, void *userdata )
{
	ThreadData* info = ( ThreadData* )userdata;
	TCHAR* type;
//	int iqId;
	// RECVED: <success ...
	// ACTION: if successfully logged in, continue by requesting roster list and set my initial status
	if (( type=JabberXmlGetAttrValue( node, "xmlns" )) == NULL ) return;

	if ( !_tcscmp( type, _T("urn:ietf:params:xml:ns:xmpp-sasl") )) {
		DBVARIANT dbv;

		JabberLog( "Success: Logged-in." );
		if ( DBGetContactSetting( NULL, jabberProtoName, "Nick", &dbv ))
			JSetStringT( NULL, "Nick", info->username );
		else
			JFreeVariant( &dbv );
		xmlStreamInitialize( "after successful sasl" );
	}
	else JabberLog( "Success: unknown action "TCHAR_STR_PARAM".",type);
}

static void JabberProcessChallenge( XmlNode *node, void *userdata )
{
	ThreadData* info = ( ThreadData* )userdata;
	if ( info->auth == NULL ) {
		JabberLog( "No previous auth have been made, exiting..." );
		return;
	}

	if ( lstrcmp( JabberXmlGetAttrValue( node, "xmlns" ), _T("urn:ietf:params:xml:ns:xmpp-sasl")))
		return;

	char* response = info->auth->getChallenge( node->text );

	XmlNode n( "response", response );
	n.addAttr( "xmlns", _T("urn:ietf:params:xml:ns:xmpp-sasl"));
	JabberSend( info->s, n );

	mir_free( response );
}

static void JabberProcessProtocol( XmlNode *node, void *userdata )
{
	ThreadData* info = ( ThreadData* ) userdata;
	if ( !strcmp( node->name, "proceed" )) {
		JabberProcessProceed( node, userdata );
		return;
	}
	
	if ( !strcmp( node->name, "stream:features" ))
		JabberProcessFeatures( node, userdata );
	else if ( !strcmp( node->name, "success"))
		JabberProcessSuccess( node, userdata );
	else if ( !strcmp( node->name, "failure"))
		JabberProcessFailure( node, userdata );
	else if ( !strcmp( node->name, "stream:error"))
		JabberProcessError( node, userdata );
	else if ( !strcmp( node->name, "challenge" ))
		JabberProcessChallenge( node, userdata );
	else if ( info->type == JABBER_SESSION_NORMAL ) {
		if ( !strcmp( node->name, "message" ))
			JabberProcessMessage( node, userdata );
		else if ( !strcmp( node->name, "presence" ))
			JabberProcessPresence( node, userdata );
		else if ( !strcmp( node->name, "iq" ))
			JabberProcessIq( node, userdata );
		else
			JabberLog( "Invalid top-level tag ( only <message/> <presence/> and <iq/> allowed )" );
	}
	else if ( info->type == JABBER_SESSION_REGISTER ) {
		if ( !strcmp( node->name, "iq" ))
			JabberProcessRegIq( node, userdata );
		else
			JabberLog( "Invalid top-level tag ( only <iq/> allowed )" );
}	}

static void JabberProcessProceed( XmlNode *node, void *userdata )
{
	ThreadData* info;
	TCHAR* type;
	node = node;
	if (( info=( ThreadData* ) userdata ) == NULL ) return;
	if (( type = JabberXmlGetAttrValue( node, "xmlns" )) != NULL && !lstrcmp( type, _T("error")))
		return;

	if ( !lstrcmp( type, _T("urn:ietf:params:xml:ns:xmpp-tls" ))) {
		JabberLog("Starting TLS...");
		int socket = JCallService( MS_NETLIB_GETSOCKET, ( WPARAM ) info->s, 0 );
		PVOID ssl;
		if (( ssl=pfn_SSL_new( jabberSslCtx )) != NULL ) {
			JabberLog( "SSL create context ok" );
			if ( pfn_SSL_set_fd( ssl, socket ) > 0 ) {
				JabberLog( "SSL set fd ok" );
				if ( pfn_SSL_connect( ssl ) > 0 ) {
					JabberLog( "SSL negotiation ok" );
					JabberSslAddHandle( info->s, ssl );	// This make all communication on this handle use SSL
					info->useSSL = true;
					JabberLog( "SSL enabled for handle = %d", info->s );
					xmlStreamInitialize( "after successful StartTLS" );
				}
				else {
					JabberLog( "SSL negotiation failed" );
					pfn_SSL_free( ssl );
			}	}
			else {
				JabberLog( "SSL set fd failed" );
				pfn_SSL_free( ssl );
}	}	}	}

static void JabberProcessMessage( XmlNode *node, void *userdata )
{
	ThreadData* info;
	XmlNode *subjectNode, *xNode, *inviteNode, *idNode, *n;
	TCHAR* from, *type, *nick, *idStr, *fromResource;
	HANDLE hContact;

	if ( !node->name || strcmp( node->name, "message" )) return;
	if (( info=( ThreadData* ) userdata ) == NULL ) return;

	type = JabberXmlGetAttrValue( node, "type" );
	if (( from = JabberXmlGetAttrValue( node, "from" )) == NULL )
		return;

	XmlNode* errorNode = JabberXmlGetChild( node, "error" );
	if ( errorNode != NULL || !lstrcmp( type, _T("error"))) {
		// we check if is message delivery failure
		int id = JabberGetPacketID( node );
		JABBER_LIST_ITEM* item = JabberListGetItemPtr( LIST_ROSTER, from );
		if ( item != NULL && id == item->idMsgAckPending ) { // yes, it is
			char *errText = t2a(JabberErrorMsg(errorNode));
			JSendBroadcast( JabberHContactFromJID( from ), ACKTYPE_MESSAGE, ACKRESULT_FAILED, ( HANDLE ) 1, (LPARAM)errText );
			mir_free(errText);
		}
		return;
	}

	JABBER_LIST_ITEM* chatItem = JabberListGetItemPtr( LIST_CHATROOM, from );
	BOOL isChatRoomJid = ( chatItem != NULL );
	if ( isChatRoomJid && !lstrcmp( type, _T("groupchat"))) {
		JabberGroupchatProcessMessage( node, userdata );
		return;
	}
	BOOL isRss = !lstrcmp( type, _T("headline"));

	// If message is from a stranger ( not in roster ), item is NULL
	JABBER_LIST_ITEM* item = JabberListGetItemPtr( LIST_ROSTER, from );

	TCHAR* szMessage = NULL;
	XmlNode* bodyNode = JabberXmlGetChild( node, "body" );
	if ( bodyNode != NULL ) {
		TCHAR* ptszBody = (bodyNode) ? bodyNode->text : (TCHAR*)_T("");
		if (( subjectNode=JabberXmlGetChild( node, "subject" ))!=NULL && subjectNode->text!=NULL && subjectNode->text[0]!='\0' && !isRss ) {
			int cbLen = _tcslen( subjectNode->text ) + _tcslen( bodyNode->text ) + 24;
			TCHAR* p = ( TCHAR* )alloca( sizeof( TCHAR ) * cbLen );
			mir_sntprintf( p, cbLen, _T("Subject: %s\r\n%s"), subjectNode->text, ptszBody );
			szMessage = p;
		}
		else szMessage = ptszBody;
	}

	time_t msgTime = 0;
	BOOL  isChatRoomInvitation = FALSE;
	TCHAR* inviteRoomJid = NULL;
	TCHAR* inviteFromJid = NULL;
	TCHAR* inviteReason = NULL;
	TCHAR* invitePassword = NULL;
	BOOL delivered = FALSE, composing = FALSE;

	n = JabberXmlGetChild( node, "active" );
	if ( item != NULL && bodyNode != NULL ) {
		if ( n != NULL && !lstrcmp( JabberXmlGetAttrValue( n, "xmlns" ), _T("http://jabber.org/protocol/chatstates")))
			item->cap |= CLIENT_CAP_CHATSTAT;
		else
			item->cap &= ~CLIENT_CAP_CHATSTAT;
	}

	n = JabberXmlGetChild( node, "composing" );
	if ( n != NULL && !lstrcmp( JabberXmlGetAttrValue( n, "xmlns" ), _T("http://jabber.org/protocol/chatstates")))
		if (( hContact = JabberHContactFromJID( from )) != NULL )
			JCallService( MS_PROTO_CONTACTISTYPING, ( WPARAM ) hContact, 60 );

	n = JabberXmlGetChild( node, "paused" );
	if ( n != NULL && !lstrcmp( JabberXmlGetAttrValue( n, "xmlns" ), _T("http://jabber.org/protocol/chatstates")))
		if (( hContact = JabberHContactFromJID( from )) != NULL )
			JCallService( MS_PROTO_CONTACTISTYPING, ( WPARAM ) hContact, PROTOTYPE_CONTACTTYPING_OFF );

	for ( int i = 1; ( xNode = JabberXmlGetNthChild( node, "x", i )) != NULL; i++ ) {
		TCHAR* ptszXmlns = JabberXmlGetAttrValue( xNode, "xmlns" );
		if ( ptszXmlns == NULL )
			continue;

		if ( !_tcscmp( ptszXmlns, _T("jabber:x:encrypted" ))) {
			if ( xNode->text == NULL )
				return;

			TCHAR* prolog = _T("-----BEGIN PGP MESSAGE-----\r\n\r\n");
			TCHAR* epilog = _T("\r\n-----END PGP MESSAGE-----\r\n");
			TCHAR* tempstring = ( TCHAR* )alloca( sizeof( TCHAR )*( _tcslen( prolog ) + _tcslen( xNode->text ) + _tcslen( epilog )));
			_tcsncpy( tempstring, prolog, _tcslen( prolog )+1 );
			_tcsncpy(tempstring + _tcslen( prolog ), xNode->text, _tcslen( xNode->text )+1);
			_tcsncpy(tempstring + _tcslen( prolog )+_tcslen(xNode->text ), epilog, _tcslen( epilog )+1);
			szMessage = tempstring;
      }
		else if ( !_tcscmp( ptszXmlns, _T("jabber:x:delay")) && msgTime == 0 ) {
			TCHAR* ptszTimeStamp = JabberXmlGetAttrValue( xNode, "stamp" );
			if ( ptszTimeStamp != NULL )
				msgTime = JabberIsoToUnixTime( ptszTimeStamp );
		}
		else if ( !_tcscmp( ptszXmlns, _T("jabber:x:event"))) {
			if ( bodyNode == NULL ) {
				idNode = JabberXmlGetChild( xNode, "id" );
				if ( JabberXmlGetChild( xNode, "delivered" ) != NULL || JabberXmlGetChild( xNode, "offline" ) != NULL ) {
					int id = -1;
					if ( idNode != NULL && idNode->text != NULL )
						if ( !_tcsncmp( idNode->text, _T(JABBER_IQID), strlen( JABBER_IQID )) )
							id = _ttoi(( idNode->text )+strlen( JABBER_IQID ));

					if ( item != NULL )
						if ( id == item->idMsgAckPending )
							JSendBroadcast( JabberHContactFromJID( from ), ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, ( HANDLE ) 1, 0 );
				}

				if ( JabberXmlGetChild( xNode, "composing" ) != NULL )
					if (( hContact = JabberHContactFromJID( from )) != NULL )
 						JCallService( MS_PROTO_CONTACTISTYPING, ( WPARAM ) hContact, 60 );

				if ( xNode->numChild==0 || ( xNode->numChild==1 && idNode != NULL ))
					// Maybe a cancel to the previous composing
					if (( hContact = JabberHContactFromJID( from )) != NULL )
						JCallService( MS_PROTO_CONTACTISTYPING, ( WPARAM ) hContact, PROTOTYPE_CONTACTTYPING_OFF );
			}
			else {
				// Check whether any event is requested
				if ( !delivered && ( n=JabberXmlGetChild( xNode, "delivered" )) != NULL ) {
					delivered = TRUE;
					idStr = JabberXmlGetAttrValue( node, "id" );

					XmlNode m( "message" ); m.addAttr( "to", from );
					XmlNode* x = m.addChild( "x" ); x->addAttr( "xmlns", "jabber:x:event" ); x->addChild( "delivered" );
					x->addChild( "id", ( idStr != NULL ) ? idStr : NULL );
					JabberSend( info->s, m );
				}
				if ( item != NULL && JabberXmlGetChild( xNode, "composing" ) != NULL ) {
					composing = TRUE;
					if ( item->messageEventIdStr )
						mir_free( item->messageEventIdStr );
					idStr = JabberXmlGetAttrValue( node, "id" );
					item->messageEventIdStr = ( idStr==NULL )?NULL:mir_tstrdup( idStr );
			}	}
		}
		else if ( !_tcscmp( ptszXmlns, _T("jabber:x:oob")) && isRss) {
			XmlNode* rssUrlNode;
			if ( (rssUrlNode = JabberXmlGetNthChild( xNode, "url", 1 )) != NULL) {
				TCHAR* ptszBody = (bodyNode) ? bodyNode->text : (TCHAR*)_T("");
				TCHAR* ptszSubject = (subjectNode) ? subjectNode->text : (TCHAR*)_T("");
				int cbLen = lstrlen( ptszBody ) + lstrlen( ptszSubject ) + lstrlen( rssUrlNode->text ) + 32;
				szMessage = ( TCHAR* )alloca( sizeof(TCHAR) * cbLen );
				mir_sntprintf( szMessage, cbLen, _T("Subject: %s\r\n"), ptszSubject );
				if ( rssUrlNode->text ) {
					_tcscat( szMessage, rssUrlNode->text );
					_tcscat( szMessage, _T("\r\n" ));
				}
				_tcscat( szMessage, ptszBody );
			}
		}
		else if ( !_tcscmp( ptszXmlns, _T("http://jabber.org/protocol/muc#user"))) {
			if (( inviteNode=JabberXmlGetChild( xNode, "invite" )) != NULL ) {
				inviteFromJid = JabberXmlGetAttrValue( inviteNode, "from" );
				if (( n=JabberXmlGetChild( inviteNode, "reason" )) != NULL )
					inviteReason = n->text;
			}

			if (( n=JabberXmlGetChild( xNode, "password" )) != NULL )
				invitePassword = n->text;
		}
		else if ( !_tcscmp( ptszXmlns, _T("jabber:x:conference"))) {
			inviteRoomJid = JabberXmlGetAttrValue( xNode, "jid" );
			if ( inviteReason == NULL )
				inviteReason = xNode->text;
			isChatRoomInvitation = TRUE;
	}	}

	if ( isChatRoomInvitation ) {
		if ( inviteRoomJid != NULL )
			JabberGroupchatProcessInvite( inviteRoomJid, inviteFromJid, inviteReason, invitePassword );
		return;
	}

	if ( bodyNode != NULL ) {
		if ( bodyNode->text == NULL )
			return;

		WCHAR* wszMessage;
		char*  szAnsiMsg;
		int    cbAnsiLen, cbWideLen;

		if (( szMessage = JabberUnixToDosT( szMessage )) == NULL )
			szMessage = mir_tstrdup( _T(""));

		#if defined( _UNICODE )
			wszMessage = szMessage; cbWideLen = wcslen( szMessage );
			cbAnsiLen = WideCharToMultiByte( CP_ACP, 0, wszMessage, cbWideLen, NULL, 0, NULL, NULL );
			szAnsiMsg = ( char* )alloca( cbAnsiLen+1 );
			WideCharToMultiByte( CP_ACP, 0, wszMessage, cbWideLen, szAnsiMsg, cbAnsiLen, NULL, NULL );
			szAnsiMsg[ cbAnsiLen ] = 0;
		#else
			szAnsiMsg = szMessage; cbAnsiLen = strlen( szMessage );
			cbWideLen = MultiByteToWideChar( CP_ACP, 0, szAnsiMsg, cbAnsiLen, NULL, 0 );
			wszMessage = ( WCHAR* )alloca( sizeof(WCHAR)*( cbWideLen+1 ));
			MultiByteToWideChar( CP_ACP, 0, szAnsiMsg, cbAnsiLen, wszMessage, cbWideLen );
			wszMessage[ cbWideLen ] = 0;
		#endif

		char* buf = ( char* )alloca( cbAnsiLen+1 + (cbWideLen+1)*sizeof( WCHAR ));
		memcpy( buf, szAnsiMsg, cbAnsiLen+1 );
		memcpy( buf + cbAnsiLen + 1, wszMessage, (cbWideLen+1)*sizeof( WCHAR ));

		HANDLE hContact = JabberHContactFromJID( from );

		if ( item != NULL ) {
			item->wantComposingEvent = composing;
			if ( hContact != NULL )
				JCallService( MS_PROTO_CONTACTISTYPING, ( WPARAM ) hContact, PROTOTYPE_CONTACTTYPING_OFF );

			// no we will monitor last resource in all modes
			if ( /*item->resourceMode==RSMODE_LASTSEEN &&*/ ( fromResource = _tcschr( from, '/' ))!=NULL ) {
				fromResource++;
				if ( *fromResource != '\0' ) {
					for ( int i=0; i<item->resourceCount; i++ ) {
						if ( !lstrcmp( item->resource[i].resourceName, fromResource )) {
							if ((item->resourceMode==RSMODE_LASTSEEN) && (i != item->lastSeenResource))
								JabberUpdateMirVer(item);
							item->lastSeenResource = i;
							putResUserSett(hContact,&item->resource[i]);
							break;
		}	}	}	}	}

		if ( hContact == NULL ) {
			// Create a temporary contact
			if ( isChatRoomJid ) {
				TCHAR* p = _tcschr( from, '/' );
				if ( p != NULL && p[1] != '\0' )
					p++;
				else
					p = from;
				hContact = JabberDBCreateContact( from, p, TRUE, FALSE );

				for ( int i=0; i < chatItem->resourceCount; i++ ) {
					if ( !lstrcmp( chatItem->resource[i].resourceName, p )) {
						JSetWord( hContact, "Status", chatItem->resource[i].status );
						break;
				}	}
			}
			else {
				nick = JabberNickFromJID( from );
				hContact = JabberDBCreateContact( from, nick, TRUE, TRUE );
				mir_free( nick );
		}	}

		time_t now = time( NULL );
		if ( msgTime == 0 || now - jabberLoggedInTime > 60 )
			msgTime = now;

		PROTORECVEVENT recv;
		recv.flags = PREF_UNICODE;
		recv.timestamp = ( DWORD )msgTime;
		recv.szMessage = buf;
		recv.lParam = 0;

		CCSDATA ccs;
		ccs.hContact = hContact;
		ccs.wParam = 0;
		ccs.szProtoService = PSR_MESSAGE;
		ccs.lParam = ( LPARAM )&recv;
		JCallService( MS_PROTO_CHAINRECV, 0, ( LPARAM )&ccs );

		mir_free( szMessage );
}	}

static void WriteStatusMessage(HANDLE hContact, TCHAR *p) {
	if (p == NULL) {
		DBDeleteContactSetting( hContact, "CList", "StatusMsg" );
		DBDeleteContactSetting( hContact, jabberProtoName, "ListeningTo" );
		return;
	}

	if (IsListeningToStatusMessage(p)) {
		DBWriteContactSettingTString( hContact, "CList", "StatusMsg", _T("") );
		DBWriteContactSettingTString( hContact, jabberProtoName, "ListeningTo", GetListeningToText(p) );
		return;
	}

	DBDeleteContactSetting( hContact, jabberProtoName, "ListeningTo" );
	DBWriteContactSettingTString( hContact, "CList", "StatusMsg", p );

}

static void JabberProcessPresence( XmlNode *node, void *userdata )
{
	ThreadData* info;
	HANDLE hContact;
	XmlNode *showNode, *statusNode;
	JABBER_LIST_ITEM *item;
	TCHAR* from, *nick, *show;
	int i;
	TCHAR* p;

	if ( !node || !node->name || strcmp( node->name, "presence" )) return;
	if (( info=( ThreadData* ) userdata ) == NULL ) return;
	if (( from = JabberXmlGetAttrValue( node, "from" )) == NULL ) return;

	if ( JabberListExist( LIST_CHATROOM, from )) {
		JabberGroupchatProcessPresence( node, userdata );
		return;
	}

	TCHAR* type = JabberXmlGetAttrValue( node, "type" );
	if ( type == NULL || !_tcscmp( type, _T("available"))) {
		if (( nick = JabberNickFromJID( from )) == NULL )
			return;

		if (( hContact = JabberHContactFromJID( from )) == NULL ) {
			if (!JabberListExist( LIST_ROSTER, from )) {
				JabberLog("SKIP Receive presence online from "TCHAR_STR_PARAM" ( who is not in my roster and not in list - skiping)", from );
				return;
			}
			hContact = JabberDBCreateContact( from, nick, TRUE, TRUE );
		}
		if ( !JabberListExist( LIST_ROSTER, from )) {
			JabberLog("Receive presence online from "TCHAR_STR_PARAM" ( who is not in my roster )", from );
			JabberListAdd( LIST_ROSTER, from );
		}
		JabberDBCheckIsTransportedContact(from,hContact);
		int status = ID_STATUS_ONLINE;
		if (( showNode = JabberXmlGetChild( node, "show" )) != NULL ) {
			if (( show = showNode->text ) != NULL ) {
				if ( !_tcscmp( show, _T("away"))) status = ID_STATUS_AWAY;
				else if ( !_tcscmp( show, _T("xa"))) status = ID_STATUS_NA;
				else if ( !_tcscmp( show, _T("dnd"))) status = ID_STATUS_DND;
				else if ( !_tcscmp( show, _T("chat"))) status = ID_STATUS_FREECHAT;
		}	}

		// Send version query if this is the new resource
		if (( p = _tcschr( from, '@' )) != NULL ) {
			if (( p = _tcschr( p, '/' ))!=NULL && p[1]!='\0' ) {
				p++;
				if (( item = JabberListGetItemPtr( LIST_ROSTER, from )) != NULL ) {
					JABBER_RESOURCE_STATUS *r = item->resource;
					for ( i=0; i < item->resourceCount && lstrcmp( r->resourceName, p ); i++, r++ );
					if ( i >= item->resourceCount || ( r->version == NULL && r->system == NULL && r->software == NULL )) {
						XmlNodeIq iq( "get", NOID, from );
						XmlNode* query = iq.addQuery( "jabber:iq:version" );
						JabberSend( info->s, iq );
		}	}	}	}

		if (( statusNode = JabberXmlGetChild( node, "status" )) != NULL && statusNode->text != NULL )
			p = mir_tstrdup( statusNode->text );
		else
			p = NULL;
		JabberListAddResource( LIST_ROSTER, from, status, p );
		WriteStatusMessage( hContact, p );
		if ( p ) {
			mir_free( p );
		}
		// Determine status to show for the contact
		if (( item=JabberListGetItemPtr( LIST_ROSTER, from )) != NULL ) {
#ifndef ORGINALRESOURCEMANAGMENT
			TCHAR *courres = JabberListGetBestClientResourceNamePtr(from);
			if (courres){ //here the best resource is chosen
				int resnum;
				if (item->resourceCount==1) resnum = 0;
				else {
					for (resnum=0;resnum<item->resourceCount;resnum++){
						if (!_tcscmp(courres,item->resource[resnum].resourceName)){
							//putResUserSett(hContact,&item->resource[resnum]);
							JabberLog("Software: " TCHAR_STR_PARAM " (" TCHAR_STR_PARAM ")",item->resource[resnum].software,item->resource[resnum].version);
							break;
						}
					}
				}
				status = item->resource[resnum].status;
				putResUserSett(hContact,&item->resource[resnum]);
			} else {
				status = status;
			}
#else
			for ( i=0; i < item->resourceCount; i++ )
				status = JabberCombineStatus( status, item->resource[i].status );
#endif
			item->status = status;
			JabberMenuUpdateSrmmIcon(item);
		}

		if ( _tcschr( from, '@' )!=NULL || JGetByte( "ShowTransport", TRUE )==TRUE )
			if ( JGetWord( hContact, "Status", ID_STATUS_OFFLINE ) != status )
				JSetWord( hContact, "Status", ( WORD )status );

		if ( _tcschr( from, '@' )==NULL && hwndJabberAgents )
			SendMessage( hwndJabberAgents, WM_JABBER_TRANSPORT_REFRESH, 0, 0 );
		JabberLog( TCHAR_STR_PARAM " ( " TCHAR_STR_PARAM " ) online, set contact status to %s", nick, from, JCallService(MS_CLIST_GETSTATUSMODEDESCRIPTION,(WPARAM)status,0 ));
		mir_free( nick );

		XmlNode* xNode;
		BOOL hasXAvatar = false;
		if (JGetByte( "EnableAvatars", TRUE )) {
			JabberLog( "Avatar enabled" );
			for ( int i = 1; ( xNode=JabberXmlGetNthChild( node, "x", i )) != NULL; i++ ) {
				if ( !lstrcmp( JabberXmlGetAttrValue( xNode, "xmlns" ), _T("jabber:x:avatar"))) {
					if (( xNode = JabberXmlGetChild( xNode, "hash" )) != NULL && xNode->text != NULL ) {
						JDeleteSetting(hContact,"AvatarXVcard");
						JabberLog( "AvatarXVcard deleted" );
						JSetStringT( hContact, "AvatarHash", xNode->text );
						hasXAvatar = true;
						DBVARIANT dbv = {0};
						int result = JGetStringT( hContact, "AvatarSaved", &dbv );
						if ( !result || lstrcmp( dbv.ptszVal, xNode->text )) {
							JabberLog( "Avatar was changed" );
							JSendBroadcast( hContact, ACKTYPE_AVATAR, ACKRESULT_STATUS, NULL, NULL );
						} else JabberLog( "Not broadcasting avatar changed" );
						if ( !result ) JFreeVariant( &dbv );
			}	}	}
			if (!hasXAvatar) { //no jabber:x:avatar. try vcard-temp:x:update
				JabberLog( "Not hasXAvatar" );
				for ( int i = 1; ( xNode=JabberXmlGetNthChild( node, "x", i )) != NULL; i++ ) {
					if ( !lstrcmp( JabberXmlGetAttrValue( xNode, "xmlns" ), _T("vcard-temp:x:update"))) {
						if (( xNode = JabberXmlGetChild( xNode, "photo" )) != NULL && xNode->text != NULL ) {
							JSetByte(hContact,"AvatarXVcard",1);
							JabberLog( "AvatarXVcard set" );
							JSetStringT( hContact, "AvatarHash", xNode->text );
							DBVARIANT dbv = {0};
							int result = JGetStringT( hContact, "AvatarSaved", &dbv );
							if ( !result || lstrcmp( dbv.ptszVal, xNode->text )) {
								JabberLog( "Avatar was changed. Using vcard-temp:x:update" );
								JSendBroadcast( hContact, ACKTYPE_AVATAR, ACKRESULT_STATUS, NULL, NULL );
							} JabberLog( "Not broadcasting avatar changed" );
							if ( !result ) JFreeVariant( &dbv );
		}	}	}	}	}
		return;
	}

	if ( !_tcscmp( type, _T("unavailable"))) {
		if ( !JabberListExist( LIST_ROSTER, from ))
			JabberLog( "SKIP Receive presence offline from " TCHAR_STR_PARAM " ( who is not in my roster )", from );
		else
			JabberListRemoveResource( LIST_ROSTER, from );

		hContact = JabberHContactFromJID( from );
		int status = ID_STATUS_OFFLINE;
		if (( statusNode = JabberXmlGetChild( node, "status" )) != NULL ) {
			if ( JGetByte( "OfflineAsInvisible", FALSE ) == TRUE )
				status = ID_STATUS_INVISIBLE;

			if (hContact != NULL) {
				WriteStatusMessage( hContact, statusNode->text );
		}	}

		if (( item=JabberListGetItemPtr( LIST_ROSTER, from )) != NULL ) {
			// Determine status to show for the contact based on the remaining resources
			status = ID_STATUS_OFFLINE;
			int resFromChosenStatus = -1;
			for ( i=0; i < item->resourceCount; i++ )
			{
				int tempStatus = status;
				status = JabberCombineStatus( status, item->resource[i].status );
				if (status != tempStatus) resFromChosenStatus = i;
			}
			if (resFromChosenStatus>-1) putResUserSett(hContact,&item->resource[resFromChosenStatus]);
			item->status = status;
			JabberMenuUpdateSrmmIcon(item);
		}
		if (( hContact=JabberHContactFromJID( from )) != NULL ) {
			if ( _tcschr( from, '@' )!=NULL || JGetByte( "ShowTransport", TRUE )==TRUE )
				if ( JGetWord( hContact, "Status", ID_STATUS_OFFLINE ) != status )
					JSetWord( hContact, "Status", ( WORD )status );

			JabberLog( TCHAR_STR_PARAM " offline, set contact status to %s", from, JCallService(MS_CLIST_GETSTATUSMODEDESCRIPTION,(WPARAM)status,0 ));
		}
		if ( _tcschr( from, '@' )==NULL && hwndJabberAgents )
			SendMessage( hwndJabberAgents, WM_JABBER_TRANSPORT_REFRESH, 0, 0 );
		JabberDBCheckIsTransportedContact(from, hContact);
		return;
	}

	if ( !_tcscmp( type, _T("subscribe"))) {
		if ( _tcschr( from, '@' ) == NULL ) {
			// automatically send authorization allowed to agent/transport
			XmlNode p( "presence" ); p.addAttr( "to", from ); p.addAttr( "type", "subscribed" );
			JabberSend( info->s, p );
		}
		else {
			if (( nick=JabberNickFromJID( from )) != NULL ) {
				JabberLog( TCHAR_STR_PARAM " ( " TCHAR_STR_PARAM " ) requests authorization", nick, from );
				JabberDBAddAuthRequest( from, nick );
				mir_free( nick );
		}	}
		return;
	}

	if ( !_tcscmp( type, _T("subscribed"))) {
		if (( item=JabberListGetItemPtr( LIST_ROSTER, from )) != NULL ) {
			if ( item->subscription == SUB_FROM ) item->subscription = SUB_BOTH;
			else if ( item->subscription == SUB_NONE ) {
				item->subscription = SUB_TO;
				if ( hwndJabberAgents && _tcschr( from, '@' )==NULL )
					SendMessage( hwndJabberAgents, WM_JABBER_TRANSPORT_REFRESH, 0, 0 );
}	}	}	}


static int JGetMirandaProductText(WPARAM wParam,LPARAM lParam)
{
	char filename[MAX_PATH],*productName;
	DWORD unused;
	DWORD verInfoSize;
	UINT blockSize;
	PVOID pVerInfo;
	GetModuleFileNameA(NULL,filename,SIZEOF(filename));
	verInfoSize=GetFileVersionInfoSizeA(filename,&unused);
	pVerInfo=mir_alloc(verInfoSize);
	GetFileVersionInfoA(filename,0,verInfoSize,pVerInfo);
	VerQueryValueA(pVerInfo,"\\StringFileInfo\\000004b0\\ProductName",(void**)&productName,&blockSize);
#if defined( _UNICODE )
	mir_snprintf(( char* )lParam, wParam, "%s", productName );
#else
	lstrcpynA((char*)lParam,productName,wParam);
#endif
	mir_free(pVerInfo);
	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////////
// Handles various <iq... requests

#ifdef __WINE__
#include <sys/utsname.h>
TCHAR *JabberReadLinuxUname()
{
	struct utsname sysinfo;
	static TCHAR str[MAX_PATH+1];

	if( uname( &sysinfo ) ) return NULL;

	mir_sntprintf( str, SIZEOF( str ), _T("%s %s"), sysinfo.sysname, sysinfo.release );

	return str;
}
#endif
static void JabberProcessIqVersion( TCHAR* idStr, XmlNode* node )
{
	TCHAR* from;
	if (( from=JabberXmlGetAttrValue( node, "from" )) == NULL )
		return;

	char* version = JabberGetVersionText();
	TCHAR* os = NULL;
#ifdef __WINE__
	TCHAR* linuxOS = JabberReadLinuxUname();
	if (!linuxOS) {
#endif
	OSVERSIONINFO osvi = { 0 };
	osvi.dwOSVersionInfoSize = sizeof( OSVERSIONINFO );
	if ( GetVersionEx( &osvi )) {
		switch ( osvi.dwPlatformId ) {
		case VER_PLATFORM_WIN32_NT:
			if ( osvi.dwMajorVersion == 5 ) {
				if ( osvi.dwMinorVersion == 2 ) os = TranslateT( "Windows Server 2003" );
				else if ( osvi.dwMinorVersion == 1 ) os = TranslateT( "Windows XP" );
				else if ( osvi.dwMinorVersion == 0 ) os = TranslateT( "Windows 2000" );
			}
			else if ( osvi.dwMajorVersion <= 4 ) {
				os = TranslateT( "Windows NT" );
			}
			break;
		case VER_PLATFORM_WIN32_WINDOWS:
			if ( osvi.dwMajorVersion == 4 ) {
				if ( osvi.dwMinorVersion == 0 ) os = TranslateT( "Windows 95" );
				if ( osvi.dwMinorVersion == 10 ) os = TranslateT( "Windows 98" );
				if ( osvi.dwMinorVersion == 90 ) os = TranslateT( "Windows ME" );
			}
			break;
	}	}

		if ( os == NULL )
#ifndef __WINE__
		os = TranslateT( "Windows" );
#else
		os = _T("Wine");
	}
#endif
	char mversion[100];
	JCallService( MS_SYSTEM_GETVERSIONTEXT, sizeof( mversion ), ( LPARAM )mversion );

#ifdef UNICODE
#  ifdef STATICSSL
	strncat(mversion," (JGmail (W,St)) ", 99-strlen(mversion));
#  else
	strncat(mversion," (JGmail (W)) ", 99-strlen(mversion));
#  endif
#else
#  ifdef STATICSSL
	strncat(mversion," (JGmail (A,St)) ", 99-strlen(mversion));
#  else
	strncat(mversion," (JGmail (A)) ", 99-strlen(mversion));
#  endif
#endif

	char mproduct[50];
	JGetMirandaProductText( sizeof( mproduct ), ( LPARAM )mproduct );

	TCHAR* fullVer = (TCHAR*)alloca(1000 * sizeof( TCHAR ));
	mir_sntprintf( fullVer, 1000, _T(TCHAR_STR_PARAM) _T(" ") _T(TCHAR_STR_PARAM) _T(" (Jabber v.") _T(TCHAR_STR_PARAM) _T(" [%s])") _T(TCHAR_STR_PARAM),
		mproduct, mversion, __VERSION_STRING, jabberThreadInfo->resource, bSecureIM ? " (SecureIM)":"" );

	XmlNodeIq iq( "result", idStr, from );	
	XmlNode* query = iq.addQuery( "jabber:iq:version" );
	query->addChild( "name", fullVer ); query->addChild( "version", version );
#ifndef __WINE__
	query->addChild( "os", os );
#else
	query->addChild( "os", linuxOS?linuxOS:os );
#endif
	JabberSend( jabberThreadInfo->s, iq );

	if ( version ) mir_free( version );
}

// last activity (XEP-0012) support
static void JabberProcessIqLast( TCHAR* idStr, XmlNode* node )
{
	TCHAR* from;
	if (( from=JabberXmlGetAttrValue( node, "from" )) == NULL )
		return;

	XmlNodeIq iq( "result", idStr, from );
	XmlNode* query = iq.addQuery( "jabber:iq:last" );
	query->addAttr("seconds", jabberIdleStartTime ? time( 0 ) - jabberIdleStartTime : 0 );
	JabberSend( jabberThreadInfo->s, iq );
}

// Returns the current GMT offset in seconds
int GetGMTOffset(void)
{
	TIME_ZONE_INFORMATION tzinfo;
	int nOffset = 0;

	DWORD dwResult= GetTimeZoneInformation(&tzinfo);

	switch(dwResult) {
	case TIME_ZONE_ID_STANDARD:
		nOffset = -(tzinfo.Bias + tzinfo.StandardBias) * 60;
		break;
	case TIME_ZONE_ID_DAYLIGHT:
		nOffset = -(tzinfo.Bias + tzinfo.DaylightBias) * 60;
		break;
	case TIME_ZONE_ID_UNKNOWN:
	case TIME_ZONE_ID_INVALID:
	default:
		nOffset = 0;
		break;
	}

	return nOffset;
}

// entity time (XEP-0202) support
static void JabberProcessIqTime202(TCHAR* idStr, XmlNode* node)
{
	TCHAR* from;
	struct tm *gmt;
	time_t ltime;
	char stime[100];
	char szTZ[10];
	if (( from = JabberXmlGetAttrValue( node, "from" )) == NULL )
		return;

	_tzset();
	time(&ltime);
	gmt = gmtime(&ltime);
	sprintf(stime,"%.4i-%.2i-%.2iT%.2i:%.2i:%.2iZ", gmt->tm_year + 1900, gmt->tm_mon, gmt->tm_mday,
		gmt->tm_hour, gmt->tm_min, gmt->tm_sec);

	int nGmtOffset = GetGMTOffset();
	ltime = abs(nGmtOffset);

	gmt = gmtime( &ltime );
	sprintf(szTZ, "%s%.2i:%.2i", nGmtOffset > 0 ? "+" : "-", gmt->tm_hour, gmt->tm_min );

	XmlNodeIq iq( "result", idStr, from );
	XmlNode* timeNode = iq.addChild( "time" );
	timeNode->addAttr( "xmlns", "urn:xmpp:time" );
	timeNode->addChild( "utc", stime);
	timeNode->addChild( "tzo", szTZ );
	JabberSend( jabberThreadInfo->s, iq);
}

static void JabberProcessIqTime( TCHAR* idStr, XmlNode* node ) //added by Rion (jep-0090)
{
	TCHAR* from;
	struct tm *gmt;
	time_t ltime;
	char stime[20],*dtime;
	if (( from = JabberXmlGetAttrValue( node, "from" )) == NULL )
		return;

	_tzset();
	time( &ltime );
	gmt = gmtime( &ltime );
	sprintf( stime,"%.4i%.2i%.2iT%.2i:%.2i:%.2i",
		gmt->tm_year+1900, gmt->tm_mon, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec );
	dtime = ctime( &ltime );
	dtime[24]=0;

	XmlNodeIq iq( "result", idStr, from );
	XmlNode* query = iq.addQuery( "jabber:iq:time" );
	query->addChild( "utc", stime ); query->addChild( "tz", _tzname[1] ); query->addChild( "display", dtime );
	JabberSend( jabberThreadInfo->s, iq );
}

static void JabberProcessIqAvatar( TCHAR* idStr, XmlNode* node )
{
	if ( !JGetByte( "EnableAvatars", TRUE ))
		return;

	TCHAR* from;
	if (( from = JabberXmlGetAttrValue( node, "from" )) == NULL )
		return;

	int pictureType = JGetByte( "AvatarType", PA_FORMAT_UNKNOWN );
	if ( pictureType == PA_FORMAT_UNKNOWN )
		return;

	char* szMimeType;
	switch( pictureType ) {
		case PA_FORMAT_JPEG:	 szMimeType = "image/jpeg";   break;
		case PA_FORMAT_GIF:	 szMimeType = "image/gif";    break;
		case PA_FORMAT_PNG:	 szMimeType = "image/png";    break;
		case PA_FORMAT_BMP:	 szMimeType = "image/bmp";    break;
		default:	return;
	}

	char szFileName[ MAX_PATH ];
	JabberGetAvatarFileName( NULL, szFileName, MAX_PATH );

	FILE* in = fopen( szFileName, "rb" );
	if ( in == NULL )
		return;

	long bytes = filelength( fileno( in ));
	char* buffer = ( char* )mir_alloc( bytes*4/3 + bytes + 1000 );
	if ( buffer == NULL ) {
		fclose( in );
		return;
	}

	fread( buffer, bytes, 1, in );
	fclose( in );

	char* str = JabberBase64Encode( buffer, bytes );
	XmlNodeIq iq( "result", idStr, from );
	XmlNode* query = iq.addQuery( "jabber:iq:avatar" );
	XmlNode* data = query->addChild( "data", str ); data->addAttr( "mimetype", szMimeType );
	JabberSend( jabberThreadInfo->s, iq );
	mir_free( str );
	mir_free( buffer );
}

static void JabberProcessIqResultVersion( TCHAR* type, XmlNode* node, XmlNode* queryNode  )
{
	TCHAR* from = JabberXmlGetAttrValue( node, "from" );
	if ( from == NULL ) return;

	JABBER_LIST_ITEM *item = NULL;

	if (( item = JabberListGetItemPtr( LIST_VCARD_TEMP, from )) == NULL)
		item = JabberListGetItemPtr( LIST_ROSTER, from );

	if ( item == NULL ) return;

	JABBER_RESOURCE_STATUS *r = item->resource;
	if ( r == NULL ) return;

	TCHAR* p = _tcschr( from, '/' );
	if ( p == NULL )    return;
	if ( *++p == '\0' ) return;

	int i;
	for ( i=0; i<item->resourceCount && _tcscmp( r->resourceName, p ); i++, r++ );
	if ( i >= item->resourceCount )
		return;

	HANDLE hContact = JabberHContactFromJID( from );
	if ( hContact == NULL )
		return;

	if ( !lstrcmp( type, _T("error"))) {
		if ( r->resourceName != NULL )
			JSetStringT( hContact, "MirVer", r->resourceName );
		return;
	}

	XmlNode* n;
	if ( r->software ) mir_free( r->software );
	if (( n=JabberXmlGetChild( queryNode, "name" ))!=NULL && n->text ) {
		if (( hContact=JabberHContactFromJID( item->jid )) != NULL ) {
			if (( p = _tcsstr( n->text, _T("Miranda IM"))) != NULL )
				JSetStringT( hContact, "MirVer", p );
			else
				JSetStringT( hContact, "MirVer", n->text );
		}
		r->software = mir_tstrdup( n->text );
	}
	else r->software = NULL;
	if ( r->version ) mir_free( r->version );
	if (( n=JabberXmlGetChild( queryNode, "version" ))!=NULL && n->text )
		r->version = mir_tstrdup( n->text );
	else
		r->version = NULL;
	if ( r->system ) mir_free( r->system );
	if (( n=JabberXmlGetChild( queryNode, "os" ))!=NULL && n->text )
		r->system = mir_tstrdup( n->text );
	else
		r->system = NULL;

	if (hContact != NULL) putResUserSett(hContact, r);

	if ( hwndJabberInfo != NULL )
		PostMessage( hwndJabberInfo, WM_JABBER_REFRESH, 0, 0);
}

static void JabberProcessIq( XmlNode *node, void *userdata )
{
	ThreadData* info;
	HANDLE hContact;
	XmlNode *queryNode, *siNode, *n, *newMailNode;
	TCHAR *from, *type, *jid, *nick;
	TCHAR *xmlns, *profile;
	TCHAR *str, *p, *q;
	TCHAR text[256];
	int i;
	JABBER_IQ_PFUNC pfunc;

	if ( !node->name || strcmp( node->name, "iq" )) return;
	if (( info=( ThreadData* ) userdata ) == NULL ) return;
	if (( type=JabberXmlGetAttrValue( node, "type" )) == NULL ) return;

	int id = JabberGetPacketID( node );
	TCHAR* idStr = JabberXmlGetAttrValue( node, "id" );

	queryNode = JabberXmlGetChild( node, "query" );
	xmlns = JabberXmlGetAttrValue( queryNode, "xmlns" );

	/////////////////////////////////////////////////////////////////////////
	// MATCH BY ID
	/////////////////////////////////////////////////////////////////////////

	if (( pfunc=JabberIqFetchFunc( id )) != NULL ) {
		JabberLog( "Handling iq request for id=%d", id );
		pfunc( node, userdata );
	}

	/////////////////////////////////////////////////////////////////////////
	// MORE GENERAL ROUTINES, WHEN ID DOES NOT MATCH
	/////////////////////////////////////////////////////////////////////////

	else if (( pfunc=JabberIqFetchXmlnsFunc( xmlns )) != NULL ) {
		JabberLog( "Handling iq request for xmlns = " TCHAR_STR_PARAM, xmlns );
		pfunc( node, userdata );
	}

	// RECVED: <iq type='set'><query ...
	else if ( !_tcscmp( type, _T("set")) && queryNode!=NULL && xmlns != NULL ) {

		// RECVED: roster push
		// ACTION: similar to iqIdGetRoster above
		if ( !_tcscmp( xmlns, _T("jabber:iq:roster"))) {
			XmlNode *itemNode;
			JABBER_LIST_ITEM *item;
			TCHAR* name;

			JabberLog( "<iq/> Got roster push, query has %d children", queryNode->numChild );
			for ( i=0; i<queryNode->numChild; i++ ) {
				itemNode = queryNode->child[i];
				if ( strcmp( itemNode->name, "item" ) != 0 )
					continue;
				if (( jid = JabberXmlGetAttrValue( itemNode, "jid" )) == NULL )
					continue;
				if (( str = JabberXmlGetAttrValue( itemNode, "subscription" )) == NULL )
					continue;

				// we will not add new account when subscription=remove
				if ( !_tcscmp( str, _T("to")) || !_tcscmp( str, _T("both")) || !_tcscmp( str, _T("from")) || !_tcscmp( str, _T("none"))) {
					if (( name=JabberXmlGetAttrValue( itemNode, "name" )) != NULL )
						nick = mir_tstrdup( name );
					else
						nick = JabberNickFromJID( jid );

					if ( nick != NULL ) {
						if (( item=JabberListAdd( LIST_ROSTER, jid )) != NULL ) {
							replaceStr( item->nick, nick );

							XmlNode* groupNode = JabberXmlGetChild( itemNode, "group" );
							replaceStr( item->group, ( groupNode ) ? groupNode->text : NULL );

							if (( hContact=JabberHContactFromJID( jid )) == NULL ) {
								// Received roster has a new JID.
								// Add the jid ( with empty resource ) to Miranda contact list.
								hContact = JabberDBCreateContact( jid, nick, FALSE, TRUE );
							}
							else JSetStringT( hContact, "jid", jid );

							if ( name != NULL ) {
								DBVARIANT dbnick;
								if ( !JGetStringT( hContact, "Nick", &dbnick )) {
									if ( _tcscmp( nick, dbnick.ptszVal ) != 0 )
										DBWriteContactSettingTString( hContact, "CList", "MyHandle", nick );
									else 
										DBDeleteContactSetting( hContact, "CList", "MyHandle" );

									JFreeVariant( &dbnick );
								}
								else DBWriteContactSettingTString( hContact, "CList", "MyHandle", nick );
							}
							else DBDeleteContactSetting( hContact, "CList", "MyHandle" );

							if ( item->group != NULL ) {
								JabberContactListCreateGroup( item->group );
								DBWriteContactSettingTString( hContact, "CList", "Group", item->group );
							}
							else DBDeleteContactSetting( hContact, "CList", "Group" );
						}
						else mir_free( nick );
				}	}

				if (( item=JabberListGetItemPtr( LIST_ROSTER, jid )) != NULL ) {
					if ( !_tcscmp( str, _T("both"))) item->subscription = SUB_BOTH;
					else if ( !_tcscmp( str, _T("to"))) item->subscription = SUB_TO;
					else if ( !_tcscmp( str, _T("from"))) item->subscription = SUB_FROM;
					else item->subscription = SUB_NONE;
					JabberLog( "Roster push for jid=" TCHAR_STR_PARAM ", set subscription to " TCHAR_STR_PARAM, jid, str );
					// subscription = remove is to remove from roster list
					// but we will just set the contact to offline and not actually
					// remove, so that history will be retained.
					if ( !_tcscmp( str, _T("remove"))) {
						if (( hContact=JabberHContactFromJID( jid )) != NULL ) {
							if ( JGetWord( hContact, "Status", ID_STATUS_OFFLINE ) != ID_STATUS_OFFLINE )
								JSetWord( hContact, "Status", ID_STATUS_OFFLINE );
							JabberListRemove( LIST_ROSTER, jid );
					}	}
					else if ( JGetByte( hContact, "ChatRoom", 0 ))
						DBDeleteContactSetting( hContact, "CList", "Hidden" );
			}	}

			if ( hwndJabberAgents )
				SendMessage( hwndJabberAgents, WM_JABBER_TRANSPORT_REFRESH, 0, 0 );
		}

		// RECVED: file transfer request
		// ACTION: notify Miranda throuch CHAINRECV
		else if ( !_tcscmp( xmlns, _T("jabber:iq:oob"))) {
			if (( jid=JabberXmlGetAttrValue( node, "from" ))!=NULL && ( n=JabberXmlGetChild( queryNode, "url" ))!=NULL && n->text!=NULL ) {
				str = n->text;	// URL of the file to get
				filetransfer* ft = new filetransfer;
				ft->std.totalFiles = 1;
				ft->jid = mir_tstrdup( jid );
				ft->std.hContact = JabberHContactFromJID( jid );
				ft->type = FT_OOB;
				ft->httpHostName = NULL;
				ft->httpPort = 80;
				ft->httpPath = NULL;
				// Parse the URL
				if ( !_tcsnicmp( str, _T("http://"), 7 )) {
					p = str + 7;
					if (( q = _tcschr( p, '/' )) != NULL ) {
						if ( q-p < SIZEOF( text )) {
							_tcsncpy( text, p, q-p );
							text[q-p] = '\0';
							if (( p = _tcschr( text, ':' )) != NULL ) {
								ft->httpPort = ( WORD )_ttoi( p+1 );
								*p = '\0';
							}
							ft->httpHostName = t2a( text );
							ft->httpPath = t2a( ++q );
				}	}	}

				if ( idStr != NULL )
					ft->iqId = mir_tstrdup( idStr );

				if ( ft->httpHostName && ft->httpPath ) {
					CCSDATA ccs;
					PROTORECVEVENT pre;
					char* szBlob, *desc;

					JabberLog( "Host=%s Port=%d Path=%s", ft->httpHostName, ft->httpPort, ft->httpPath );
					if (( n=JabberXmlGetChild( queryNode, "desc" ))!=NULL && n->text!=NULL )
						desc = t2a( n->text );
					else
						desc = mir_strdup( "" );

					if ( desc != NULL ) {
						char* str;
						JabberLog( "description = %s", desc );
						if (( str = strrchr( ft->httpPath, '/' )) != NULL )
							str++;
						else
							str = ft->httpPath;
						str = mir_strdup( str );
						JabberHttpUrlDecode( str );
						szBlob = ( char* )mir_alloc( sizeof( DWORD )+ strlen( str ) + strlen( desc ) + 2 );
						*(( PDWORD ) szBlob ) = ( DWORD )ft;
						strcpy( szBlob + sizeof( DWORD ), str );
						strcpy( szBlob + sizeof( DWORD )+ strlen( str ) + 1, desc );
						pre.flags = 0;
						pre.timestamp = time( NULL );
						pre.szMessage = szBlob;
						pre.lParam = 0;
						ccs.szProtoService = PSR_FILE;
						ccs.hContact = ft->std.hContact;
						ccs.wParam = 0;
						ccs.lParam = ( LPARAM )&pre;
						JCallService( MS_PROTO_CHAINRECV, 0, ( LPARAM )&ccs );
						mir_free( szBlob );
						mir_free( str );
						mir_free( desc );
					}
				}
				else {
					// reject
					XmlNodeIq iq( "error", idStr, ft->jid );
					XmlNode* e = iq.addChild( "error", "File transfer refused" ); e->addAttr( "code", 406 );
					JabberSend( jabberThreadInfo->s, iq );
					delete ft;
		}	}	}

		// RECVED: bytestream initiation request
		// ACTION: check for any stream negotiation that is pending ( now only file transfer is handled )
		else if ( !_tcscmp( xmlns, _T("http://jabber.org/protocol/bytestreams")))
			JabberFtHandleBytestreamRequest( node );
	}
	// RECVED: <iq type='get'><query ...
	else if ( !_tcscmp( type, _T( "get" ))) {
		// RECVED: software version query
		// ACTION: return my software version
		if ( queryNode != NULL && xmlns != NULL ) {
			if ( !_tcscmp( xmlns, _T("jabber:iq:version")))
				JabberProcessIqVersion( idStr, node );
			else if ( !_tcscmp( xmlns, _T("jabber:iq:avatar")))
				JabberProcessIqAvatar( idStr, node );
			else if ( !_tcscmp( xmlns, _T("jabber:iq:time")))
				JabberProcessIqTime( idStr, node );	
			else if ( !_tcscmp( xmlns, _T("jabber:iq:last")))
				JabberProcessIqLast( idStr, node );
		}
		else {
			// entity time (XEP-0202) support
			XmlNode* timeNode = JabberXmlGetChildWithGivenAttrValue( node, "time", "xmlns", _T( "urn:xmpp:time" ));
			if ( timeNode )
				JabberProcessIqTime202( idStr, node );
		}
	}
	// RECVED: <iq type='result'><query ...
	else if ( !_tcscmp( type, _T("result")) && queryNode != NULL && xmlns != NULL ) {

		// RECVED: software version result
		// ACTION: update version information for the specified jid/resource
		if ( !_tcscmp( xmlns, _T("jabber:iq:version")))
			JabberProcessIqResultVersion( type, node, queryNode );
	}
	// RECVED: <iq type='set'><si xmlns='http://jabber.org/protocol/si' ...
	else if ( !_tcscmp( type, _T("set")) && ( siNode=JabberXmlGetChildWithGivenAttrValue( node, "si", "xmlns", _T("http://jabber.org/protocol/si")))!=NULL && ( profile=JabberXmlGetAttrValue( siNode, "profile" ))!=NULL ) {

		// RECVED: file transfer request
		// ACTION: notify Miranda throuch CHAINRECV
		if ( !_tcscmp( profile, _T("http://jabber.org/protocol/si/profile/file-transfer" ))) {
			JabberFtHandleSiRequest( node );
		}
		// RECVED: unknown profile
		// ACTION: reply with bad-profile
		else {
			if (( from=JabberXmlGetAttrValue( node, "from" )) != NULL ) {
				XmlNodeIq iq( "error", idStr, from );
				XmlNode* error = iq.addChild( "error" ); error->addAttr( "code", "400" ); error->addAttr( "type", "cancel" );
				XmlNode* brq = error->addChild( "bad-request" ); brq->addAttr( "xmlns", "urn:ietf:params:xml:ns:xmpp-stanzas" );
				XmlNode* bp = error->addChild( "bad-profile" ); brq->addAttr( "xmlns", "http://jabber.org/protocol/si" );
				JabberSend( jabberThreadInfo->s, iq );
		}	}
	}
    // RECVED: <iq type=\"set\"><new-mail \" ...
	else if ( !_tcscmp( type, _T("set") ) && ( newMailNode=JabberXmlGetChild( node, "new-mail" ) ) ) {
		// RECVED: new-mail notify
		// ACTION: Reply & request
		if (JGetByte(NULL,"EnableGMail",1) & 1) {
			XmlNode iq( "iq" ); iq.addAttr( "type", "result" ); iq.addAttr ("id",JabberXmlGetAttrValue( node, "id" ) );
			JabberSend( jabberThreadInfo->s, iq );
			JabberRequestMailBox( info->s );
	}	}
    // RECVED: <iq type=\"set\"><usersetting \" ...
	else if ( (!_tcscmp( type, _T("set") ) || !_tcscmp( type, _T("result")))  && ( newMailNode=JabberXmlGetChild( node, "usersetting" ) ) ) {
		// RECVED: usersettings result/set
		// ACTION: if "set": reply; parse the settings always.
		if (!_tcscmp( type, _T("set"))) {
			XmlNode iq( "iq" ); iq.addAttr( "type", "result" ); iq.addAttr ("id",JabberXmlGetAttrValue( node, "id" ) );
			JabberSend( jabberThreadInfo->s, iq );
		}
		JabberUserConfigResult(node,jabberThreadInfo);
	}
	// RECVED: <iq type='error'> ...
	else if ( !_tcscmp( type, _T("error"))) {
		if ( !lstrcmp( xmlns, _T("jabber:iq:version"))) {
			JabberProcessIqResultVersion( type, node, queryNode );
			return;
		}
		JabberLog( "XXX on entry" );
		// Check for file transfer deny by comparing idStr with ft->iqId
		i = 0;
		while (( i=JabberListFindNext( LIST_FILE, i )) >= 0 ) {
			JABBER_LIST_ITEM *item = JabberListGetItemPtrFromIndex( i );
			if ( item->ft != NULL && item->ft->state == FT_CONNECTING && !_tcscmp( idStr, item->ft->iqId )) {
				JabberLog( "Denying file sending request" );
				item->ft->state = FT_DENIED;
				if ( item->ft->hFileEvent != NULL )
					SetEvent( item->ft->hFileEvent );	// Simulate the termination of file server connection
			}
			i++;
}	}	}

static void JabberProcessRegIq( XmlNode *node, void *userdata )
{
	ThreadData* info;
	XmlNode *errorNode;
	TCHAR *type, *str;

	if ( !node->name || strcmp( node->name, "iq" )) return;
	if (( info=( ThreadData* ) userdata ) == NULL ) return;
	if (( type=JabberXmlGetAttrValue( node, "type" )) == NULL ) return;

	int id = JabberGetPacketID( node );

	if ( !_tcscmp( type, _T("result"))) {

		// RECVED: result of the request for registration mechanism
		// ACTION: send account registration information
		if ( id == iqIdRegGetReg ) {
			iqIdRegSetReg = JabberSerialNext();

			XmlNodeIq iq( "set", iqIdRegSetReg );
			XmlNode* query = iq.addQuery( "jabber:iq:register" );
			query->addChild( "password", info->password );
			query->addChild( "username", info->username );
			JabberSend( info->s, iq );

			SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 75, ( LPARAM )TranslateT( "Sending registration information..." ));
		}
		// RECVED: result of the registration process
		// ACTION: account registration successful
		else if ( id == iqIdRegSetReg ) {
			JabberSend( info->s, "</stream:stream>" );
			SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, ( LPARAM )TranslateT( "Registration successful" ));
			info->reg_done = TRUE;
	}	}

	else if ( !_tcscmp( type, _T("error"))) {
		errorNode = JabberXmlGetChild( node, "error" );
		str = JabberErrorMsg( errorNode );
		SendMessage( info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, ( LPARAM )str );
		mir_free( str );
		info->reg_done = TRUE;
		JabberSend( info->s, "</stream:stream>" );
}	}

/////////////////////////////////////////////////////////////////////////////////////////
// ThreadData constructor & destructor

ThreadData::ThreadData( JABBER_SESSION_TYPE parType )
{
	memset( this, 0, sizeof( *this ));
	type = parType;
	caps = CAPS_BOOKMARK; // assume that the server supports bookmarks
}

ThreadData::~ThreadData()
{
	if ( s )
		Netlib_CloseHandle(s);

	delete auth;
}

