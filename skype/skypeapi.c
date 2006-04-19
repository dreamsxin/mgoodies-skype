/*
 * SkypeAPI - All more or less important functions that deal with Skype
 */

#include "skype.h"
#include "skypeapi.h"
#include "debug.h"
#include "contacts.h"
#include "skypeproxy.h"
#include "pthread.h"
#include "../headers_c/m_utils.h"
#include "../headers_c/m_langpack.h"
#include "m_toptoolbar.h"

// Imported Globals
extern HWND hSkypeWnd, hWnd;
extern BOOL SkypeInitialized, UseSockets;
extern int SkypeStatus, receivers;
extern HANDLE SkypeReady, SkypeMsgReceived, httbButton;
extern char pszSkypeProtoName[MAX_PATH+30];
extern UINT ControlAPIAttach, ControlAPIDiscover;
extern LONG AttachStatus;
extern HINSTANCE hInst;
extern PLUGININFO pluginInfo;


// -> Skype Message Queue functions //

struct MsgQueue *SkypeMsgs=NULL; 

status_map status_codes[] = {
	{ID_STATUS_AWAY, "AWAY"},
	{ID_STATUS_NA, "NA"},
	{ID_STATUS_DND, "DND"},
	{ID_STATUS_ONLINE, "ONLINE"},
	{ID_STATUS_FREECHAT, "SKYPEME"},	// Unfortunately Skype API tells us userstatus ONLINE, if we are free for chat
	{ID_STATUS_OFFLINE, "OFFLINE"},
	{ID_STATUS_INVISIBLE, "INVISIBLE"},
	{ID_STATUS_CONNECTING, "CONNECTING"},
	{0, NULL}
};

CRITICAL_SECTION MsgQueueMutex;
BOOL rcvThreadRunning=FALSE;
SOCKET ClientSocket=INVALID_SOCKET;

/*
 * Skype via Socket --> Skype2Socket connection
 */

void rcvThread(char *dummy) {
	unsigned int length;
	char *buf;
	COPYDATASTRUCT CopyData;

	if (!UseSockets) return;
	rcvThreadRunning=TRUE;
	while (1) {
		if (ClientSocket==INVALID_SOCKET) {
			return;
			rcvThreadRunning=FALSE;
		}
		LOG("rcvThread", "Receiving from socket..");
		if (recv(ClientSocket, (char *)&length, sizeof(length), 0)==SOCKET_ERROR) {
			rcvThreadRunning=FALSE;
			LOG("rcvThread", "Socket error");
			return;
		}
		LOG("rcvThread", "Received length, recieving message..");
		buf=(char *)calloc(1, length+1);
		if (recv(ClientSocket, buf, length, 0)==SOCKET_ERROR) {
			rcvThreadRunning=FALSE;
			LOG("rcvThread", "Socket error");
			free(buf);
			return;
		}
		LOG("Received message: ", buf);

		CopyData.dwData=0; 
		CopyData.lpData=buf; 
		CopyData.cbData=strlen(buf)+1;
		if (!SendMessage(hWnd, WM_COPYDATA, (WPARAM)hSkypeWnd, (LPARAM)&CopyData))
			LOGL("SendMessage failed: ", GetLastError());

		free(buf);
	}
}

/*
 * Skype Messagequeue - Implemented as a linked list 
 */

/* SkypeMsgInit
 * 
 * Purpose: Initializes the Skype Message queue
 * Returns: 0 - Success
 *         -1 - Memory allocation failure
 */
int SkypeMsgInit(void) {
	
	if ((SkypeMsgs=(struct MsgQueue*)malloc(sizeof(struct MsgQueue)))==NULL) return -1;
	SkypeMsgs->message=NULL;
	SkypeMsgs->tAdded=0;
	SkypeMsgs->next=NULL;
	InitializeCriticalSection(&MsgQueueMutex);
	return 0;
}

/* SkypeMsgAdd
 * 
 * Purpose: Add Message to linked list
 * Params : msg - Message to add to queue
 * Returns: 0 - Success
 *         -1 - Memory allocation failure
 */
int SkypeMsgAdd(char *msg) {
	struct MsgQueue *ptr;

	EnterCriticalSection(&MsgQueueMutex);
	ptr=SkypeMsgs;
	if (!ptr) return -1;
	while (ptr->next!=NULL) ptr=ptr->next;
	if ((ptr->next=(struct MsgQueue*)malloc(sizeof(struct MsgQueue)))==NULL) return -1;
	ptr=ptr->next;
	ptr->message = strdup(msg); // Don't forget to free!
	ptr->tAdded = time(NULL);
	ptr->next=NULL;
	LeaveCriticalSection(&MsgQueueMutex);
	return 0;
}

/* SkypeMsgCleanup
 * 
 * Purpose: Clean up the whole MESSagequeue - free() all
 */
void SkypeMsgCleanup(void) {
	struct MsgQueue *ptr;

	LOG("SkypeMsgCleanup", "Cleaning up message queue..");
	EnterCriticalSection(&MsgQueueMutex);
	if (SkypeMsgs) SkypeMsgs->message=NULL; //First message is free()d by user
	while (SkypeMsgs) {
		ptr=SkypeMsgs;
		if (ptr->message) free(ptr->message);
		SkypeMsgs=ptr->next;
		free(ptr);
	}
	LeaveCriticalSection(&MsgQueueMutex);
	LOG("SkypeMsgCleanup", "Done.");
}

/* SkypeMsgGet
 * 
 * Purpose: Fetch next message from message queue
 * Returns: The next message
 * Warning: Don't forget to free() return value!
 */
char *SkypeMsgGet(void) {
	struct MsgQueue *ptr;
	char *msg;

	if (!SkypeMsgs || !((ptr=SkypeMsgs)->next)) return NULL;
	EnterCriticalSection(&MsgQueueMutex);
	msg=ptr->next->message;
	SkypeMsgs=ptr->next;
	free(ptr);
	LeaveCriticalSection(&MsgQueueMutex);
	return msg;
}

// Message sending routine, for internal use by SkypeSend
int __sendMsg(char *szMsg) {
   COPYDATASTRUCT CopyData;
   LRESULT SendResult;
   int oldstatus;
   unsigned int length=strlen(szMsg);

   LOG(">", szMsg);
   if (UseSockets) {

	   if (ClientSocket==INVALID_SOCKET) return -1;
	   // Fake PING-PONG, as PING-PONG is not supported by Skype2Socket
	   if (!strcmp(szMsg, "PING")) {
		 char *buf="PONG";

		 CopyData.dwData=0; 
		 CopyData.lpData=buf; 
		 CopyData.cbData=strlen(buf)+1;
		 SendMessage(hWnd, WM_COPYDATA, (WPARAM)hSkypeWnd, (LPARAM)&CopyData);
		 return 0;
	   } else
	   if (send(ClientSocket, (char *)&length, sizeof(length), 0)==SOCKET_ERROR ||
		   send(ClientSocket, szMsg, length, 0)==SOCKET_ERROR) SendResult=0;
	   else return 0;
   } else {
	   CopyData.dwData=0; 
	   CopyData.lpData=szMsg; 
	   CopyData.cbData=strlen(szMsg)+1;
       if (!hSkypeWnd) LOG("SkypeSend", "DAMN! No Skype window handle! :(");
	   SendResult=SendMessage(hSkypeWnd, WM_COPYDATA, (WPARAM)hWnd, (LPARAM)&CopyData);
       LOGL("SkypeSend: SendMessage returned ", SendResult);
   }
   if (!SendResult) {
	  SkypeInitialized=FALSE;
  	  if (SkypeStatus!=ID_STATUS_OFFLINE) {
		// Go offline
		logoff_contacts();
		oldstatus=SkypeStatus;
		SkypeStatus=ID_STATUS_OFFLINE;
		ProtoBroadcastAck(pszSkypeProtoName, NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE) oldstatus, SkypeStatus);
	  }
	  // Reconnect to Skype
	  ResetEvent(SkypeReady);
	  if (ConnectToSkypeAPI(NULL)!=-1) {
		  if (UseSockets) {
		  	   if (send(ClientSocket, (char *)&length, sizeof(length), 0)==SOCKET_ERROR ||
			   send(ClientSocket, szMsg, length, 0)==SOCKET_ERROR) return -1;
		  } else
			if (!SendMessage(hSkypeWnd, WM_COPYDATA, (WPARAM)hWnd, (LPARAM)&CopyData)) return -1;
		  pthread_create(SkypeSystemInit, NULL);
	  } else return -1;

//	  SendMessageTimeout(HWND_BROADCAST, ControlAPIDiscover, (WPARAM)hWnd, 0, SMTO_ABORTIFHUNG, 3000, NULL);
   }
   return 0;
}


/* SkypeSend
 * 
 * Purpose: Sends the specified message to the Skype API.
 *		    If it fails, try to reconnect zu the Skype API
 * Params:  use like sprintf without first param (dest. buffer)
 * Returns: 0 - Success
 *		   -1 - Failure
 */
int SkypeSend(char *szFmt, ...) {
   char *szMsg, *pChr=szFmt;
   int numArgs=0, i;
   unsigned int length=strlen(szFmt);
   va_list ap;
   

   // 0.0.0.17+  - Build message-String from supplied parameter list
   // so the user doesn't have to care about memory allocation any more.
   // BEWARE: This function ONLY works for STRINGS (%s)
   //         Don't try to supply other parameters than strings
   //         The string-Identifier %s has to be lower-case!
   // You can optimize the speed of this function by using _alloca,
   // because stack-allocation is fast and you don't have to free it
   // on returning, because the stack-frame is destroyed anyways.
   // However this gets dangerous, when you pass large strings to this
   // function, so I decided to keep malloc() heap-allocation

   while (pChr=strstr(pChr, "%s")) { numArgs++; pChr++; }
   if (numArgs) {
	   char *pArg;

	   // Calculate size of buffer
	   va_start(ap, szFmt);
	   for (i=0; i<numArgs; i++) {
		   pArg = va_arg(ap, char*);
		   if (pArg) length+=strlen(pArg)-2;
	   }
	   va_end(ap);

	   // Now allocate buffer
	   length++;
//	   LOGL("Allocating buffer of size", length);
	   if (!(szMsg=malloc(length))) return -1;

	   va_start(ap, szFmt);
         _vsnprintf(szMsg, length, szFmt, ap);
	   va_end(ap);
   } else return __sendMsg(szFmt);
   
   i=__sendMsg(szMsg);
   free(szMsg);
   return i;
}

/* SkypeRcv
 * 
 * Purpose: Wait, until either the messge "what" is received or maxwait-Time has passed
 *		    or there was an error and return it
 * Params : what	- Wait for this string-part at the beginning of a received string
 *					  If the first character of the string is NULL, the rest after the NULL
 *					  character will be searched in the entire received message-string
 *		    maxwait - Wait this time before returning, if nothing was received,
 *					  can be INFINITE
 * Returns: The received message containing "what" or a ERROR-Message or NULL if 
 *			time is up and nothing was received
 * Warning: Don't forget to free() return value!
 */
char *SkypeRcv(char *what, DWORD maxwait) {
	char *msg;
	struct MsgQueue *ptr, *ptr_;

	do {
		EnterCriticalSection(&MsgQueueMutex);
		ptr_=SkypeMsgs;
		ptr=ptr_->next;
		while (ptr!=NULL) {
			if (what==NULL || !strncmp(ptr->message, what, strlen(what)) ||
				(what[0]==0 && strstr(ptr->message, what+1)) || 
				!strncmp(ptr->message, "ERROR", 5)) 
			{
				msg=ptr->message;
				ptr_->next=ptr->next;
				LOG("<SkypeRcv:", msg);
				free(ptr);
				LeaveCriticalSection(&MsgQueueMutex);
				return msg;
			}
			ptr_=ptr;
			ptr=ptr_->next;
		}
		LeaveCriticalSection(&MsgQueueMutex);
		receivers++;
		if (WaitForSingleObject(SkypeMsgReceived, maxwait)!=WAIT_OBJECT_0) break;
		if (receivers>1) receivers--;
		
	} while(1);	
	return NULL;
}
/*
  Introduced in 0.0.0.17
  
  Issues a GET szWhat szWho szProperty and waits until the answer is received
  Returns the answer or NULL on failure
  BEWARE: Don't forget to free() return value!

  For example:  SkypeGet("USER", dbv.pszVal, "FULLNAME");
*/ 
char *SkypeGet(char *szWhat, char *szWho, char *szProperty) {
  char *str, *ptr;
  int retval, len;

  if (!(str=malloc(strlen(szWhat)+strlen(szWho)+strlen(szProperty)+7))) return NULL;
  sprintf(str, "GET %s %s %s", szWhat, szWho, szProperty);
  retval=__sendMsg(str);
  if (retval) {
	  free(str);
	  return NULL;
  }
  len=strlen(str+3);
  if (!szProperty[0]) len--;
  ptr = SkypeRcv(str+4, INFINITE);
  LOG("SkypeGet - Request", str);
  free(str);
  if (ptr) memmove(ptr, ptr+len, strlen(ptr)-len+1);
  LOG("SkypeGet - Answer", ptr);
  return ptr;
}

/* SkypeMsgCollectGarbage
 * 
 * Purpose: Runs the garbage collector on the Skype Message-Queue to throw out old 
 *		    messages which may unnecessarily eat up memory.
 * Params : age     - Time in seconds. Messages older than this value will be 
 *					  thrown out.
 * Returns: 0  - No messages were thrown out
 *			>0 - n messages were thrown out
 */
int SkypeMsgCollectGarbage(time_t age) {
	struct MsgQueue *ptr, *ptr_;
	int i=0;

	EnterCriticalSection(&MsgQueueMutex);
	ptr_=SkypeMsgs;
	ptr=ptr_->next;
	while (ptr!=NULL) {
		if (ptr->tAdded && time(NULL)-ptr->tAdded>age) {
			LOG("GarbageCollector throwing out message: ", ptr->message);
			free(ptr->message);
			ptr_->next=ptr->next;
			free(ptr);
			ptr=ptr_;
			i++;
		}
		ptr_=ptr;
		ptr=ptr_->next;
	}
	LeaveCriticalSection(&MsgQueueMutex);
	return i;
}


/* SkypeCall
 * 
 * Purpose: Give a Skype call to the given User in wParam
 *          or hangs up existing call
 * Params : wParam - Handle to the User to be called
 *			lParam - Can be NULL
 * Returns: 0 - Success
 *		   -1 - Failure
 */
int SkypeCall(WPARAM wParam, LPARAM lParam) {
	DBVARIANT dbv;
	char *msg;
	int res;

	if (!DBGetContactSetting((HANDLE)wParam, pszSkypeProtoName, "CallId", &dbv)) {
		msg=(char *)malloc(strlen(dbv.pszVal)+21);
		sprintf(msg, "SET %s STATUS FINISHED", dbv.pszVal);
		res=SkypeSend(msg);
	} else {
		if (DBGetContactSetting((HANDLE)wParam, pszSkypeProtoName, SKYPE_NAME, &dbv)) return -1;
		msg=(char *)malloc(strlen(dbv.pszVal)+6);
		strcpy(msg, "CALL ");
		strcat(msg, dbv.pszVal);
		res=SkypeSend(msg);
	}
	DBFreeVariant(&dbv);
	free(msg);
	return res;
}

/* FixNumber
 * 
 * Purpose: Eliminates all non-numeric chars from the given phonenumber
 * Params : p	- Pointer to the buffer with the number
 */
void FixNumber(char *p) {
	unsigned int i;

	for (i=0;i<=strlen(p);i++)
		if ((p[i]<'0' || p[i]>'9')) 
			if (p[i]) {
				memmove(p+i, p+i+1, strlen(p+i));
				i--;
			} else break;
}


/* DialDlgProc
 * 
 * Purpose: Dialog procedure for the Dial-Dialog
 */
static int CALLBACK DialDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static HANDLE hContact;
	static unsigned int entries=0;
	
	switch (uMsg){
		case WM_INITDIALOG:	
			hContact=(HANDLE)lParam;
			Utils_RestoreWindowPosition(hwndDlg, NULL, pszSkypeProtoName, "DIALdlg");
			TranslateDialogDefault(hwndDlg);

			if (lParam) {
				DBVARIANT dbv;
				BOOL bDialNow=TRUE;

				if (!DBGetContactSetting(hContact,"UserInfo","MyPhone1",&dbv)) {
					int j;
					char idstr[16];

					// Multiple phone numbers, select one
					bDialNow=FALSE;
					DBFreeVariant(&dbv);
					for(j=0;;j++) {
						wsprintf(idstr,"MyPhone%d",j);
						if(DBGetContactSetting(hContact,"UserInfo",idstr,&dbv)) break;
						FixNumber(dbv.pszVal+1); // Leave + alone
						SendDlgItemMessage(hwndDlg,IDC_NUMBER,CB_ADDSTRING,0,(LPARAM)dbv.pszVal);
						DBFreeVariant(&dbv);
					}
				}
				if (DBGetContactSetting(hContact,pszSkypeProtoName,"SkypeOutNr",&dbv)) {
					DBGetContactSetting(hContact,"UserInfo","MyPhone0",&dbv);
					FixNumber(dbv.pszVal+1);
				}
				SetDlgItemText(hwndDlg, IDC_NUMBER, dbv.pszVal);
				DBFreeVariant(&dbv);
				if (bDialNow) PostMessage(hwndDlg, WM_COMMAND, IDDIAL, 0);
			} else {
				DBVARIANT dbv;
				char number[64];

				for (entries=0;entries<MAX_ENTRIES;entries++) {
					_snprintf(number, sizeof(number), "LastNumber%d", entries);
					if (!DBGetContactSetting(NULL, pszSkypeProtoName, number, &dbv)) {
						SendDlgItemMessage(hwndDlg,IDC_NUMBER,CB_ADDSTRING,0,(LPARAM)dbv.pszVal);
						DBFreeVariant(&dbv);
					} else break	;
				}
			}
			SetFocus(GetDlgItem(hwndDlg, IDC_NUMBER));
			return TRUE;
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				char number[64], *msg, *ptr=NULL;
				BOOL TempAdded=FALSE;

				case IDDIAL:
					EnableWindow(GetDlgItem(hwndDlg, IDDIAL), FALSE);
					GetDlgItemText(hwndDlg, IDC_NUMBER, number, sizeof(number));
					if (!strncmp(number, "00", 2)) {					
						memmove(number, number+1, sizeof(number)-1);
						number[0]='+';
						number[sizeof(number)]=0;
					}
					if (!hContact) {
						if (!(hContact=add_contact(number, PALF_TEMPORARY))) {
							DestroyWindow(hwndDlg);
							break;
						}
						DBDeleteContactSetting(hContact, "CList", "Hidden");
						DBWriteContactSettingWord(hContact, pszSkypeProtoName, "Status", (WORD)SkypeStatusToMiranda("SKYPEOUT"));
						if (SendDlgItemMessage(hwndDlg,IDC_NUMBER,CB_FINDSTRING,0,(LPARAM)number)==CB_ERR) {
							int i;
							char buf[64];
							DBVARIANT dbv;

							if (entries>MAX_ENTRIES) entries=MAX_ENTRIES;
							for (i=entries;i>0;i--) {
								_snprintf(buf, sizeof(buf), "LastNumber%d", i-1);
								if (!DBGetContactSetting(NULL, pszSkypeProtoName, buf, &dbv)) {
									_snprintf(buf, sizeof(buf), "LastNumber%d", i);
									DBWriteContactSettingString(NULL, pszSkypeProtoName, buf, dbv.pszVal);
									DBFreeVariant(&dbv);
								} else break;
							}
							DBWriteContactSettingString(NULL, pszSkypeProtoName, "LastNumber0", number);
						}
						TempAdded=TRUE;
					}
					if (!DBWriteContactSettingString(hContact, pszSkypeProtoName, "SkypeOutNr", number)) {
						msg=(char *)malloc(strlen(number)+6);
						strcpy(msg, "CALL ");
						strcat(msg, number);
						if (SkypeSend(msg) || (ptr=SkypeRcv("ERROR", 500))) {
							DBDeleteContactSetting(hContact, pszSkypeProtoName, "SkypeOutNr");
							if (ptr) {
								OUTPUT(ptr);
								free(ptr);
							}
							if (TempAdded) CallService(MS_DB_CONTACT_DELETE, (WPARAM)hContact, 0);
						}
						free(msg);
					}
				case IDCANCEL:
					DestroyWindow(hwndDlg);
					break;
			}			
			break;
		case WM_DESTROY:
			Utils_SaveWindowPosition(hwndDlg, NULL, pszSkypeProtoName, "DIALdlg");
			if (httbButton) CallService(MS_TTB_SETBUTTONSTATE, (WPARAM)httbButton, TTBST_RELEASED);
			break;
	}
	return 0;
}

/* CallstatDlgProc
 * 
 * Purpose: Dialog procedure for the CallStatus Dialog
 */
static int CALLBACK CallstatDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static int selected;
	static DBVARIANT dbv, dbv2={0};
	char *msg=NULL;

	switch (uMsg){
		case WM_INITDIALOG:	
		{
			HANDLE hContact;
			char *szProto;

			if (!DBGetContactSetting((HANDLE)lParam, pszSkypeProtoName, "CallId", &dbv)) {

				// Check, if another call is in progress
				for (hContact=(HANDLE)CallService(MS_DB_CONTACT_FINDFIRST, 0, 0);hContact != NULL;hContact=(HANDLE)CallService( MS_DB_CONTACT_FINDNEXT, (WPARAM)hContact, 0)) {
					szProto = (char*)CallService( MS_PROTO_GETCONTACTBASEPROTO, (WPARAM)hContact, 0 );
					if (szProto!=NULL && !strcmp(szProto, pszSkypeProtoName) && hContact!=(HANDLE)lParam &&
						DBGetContactSettingByte(hContact, pszSkypeProtoName, "ChatRoom", 0) == 0 &&
						!DBGetContactSetting(hContact, pszSkypeProtoName, "CallId", &dbv2)) 
					{
						if (DBGetContactSettingByte(hContact, pszSkypeProtoName, "OnHold", 0)) {
							DBFreeVariant(&dbv2);
							continue;
						} else break;
					}
				}
	
				if (dbv2.pszVal)
				{
					char buf[256], buf2[256];
					char *szOtherCaller=(char *)CallService(MS_CLIST_GETCONTACTDISPLAYNAME,(WPARAM)hContact,0);

					Utils_RestoreWindowPosition(hwndDlg, NULL, pszSkypeProtoName, "CALLSTATdlg");
					TranslateDialogDefault(hwndDlg);
					SendMessage(hwndDlg, WM_COMMAND, IDC_JOIN, 0);

					GetWindowText(hwndDlg, buf, sizeof(buf));
					_snprintf(buf2, sizeof(buf), buf, CallService(MS_CLIST_GETCONTACTDISPLAYNAME,(WPARAM)lParam,0));
					SetWindowText(hwndDlg, buf2);
					
					GetDlgItemText(hwndDlg, IDC_JOIN, buf, sizeof(buf));
					_snprintf(buf2, sizeof(buf), buf, szOtherCaller);
					SetDlgItemText(hwndDlg, IDC_JOIN, buf2);

					GetDlgItemText(hwndDlg, IDC_HOLD, buf, sizeof(buf));
					_snprintf(buf2, sizeof(buf), buf, szOtherCaller);
					SetDlgItemText(hwndDlg, IDC_HOLD, buf2);

					return TRUE;
				}

				// No other call in progress, no need for this Dlg., just answer the call
				if (msg=(char *)malloc(strlen(dbv.pszVal)+23)) {
					sprintf(msg, "SET %s STATUS INPROGRESS", dbv.pszVal);
					SkypeSend(msg);
					free(msg);
				}
				DBFreeVariant(&dbv);
			}
			DestroyWindow(hwndDlg);
			break;
		}
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDC_JOIN:
				case IDC_HOLD:
				case IDC_HANGUP:
					CheckRadioButton(hwndDlg, IDC_JOIN, IDC_HANGUP, (selected=LOWORD(wParam)));
					break;
				case IDOK:
				{
					char *szIdCall2;

					switch (selected) {
						case IDC_JOIN: 
							if ((szIdCall2=strchr(dbv2.pszVal, ' ')) &&
								(msg=(char *)malloc(strlen(dbv.pszVal)+strlen(dbv2.pszVal)+18))
							   ) 
								sprintf(msg, "SET %s JOIN_CONFERENCE%s", dbv.pszVal, szIdCall2);
							break;
						case IDC_HOLD:
							if (msg=malloc(strlen(dbv2.pszVal)+20)) {
								sprintf(msg, "SET %s STATUS ONHOLD", dbv2.pszVal);
								SkypeSend(msg);
								free(msg);
								if (msg=malloc(strlen(dbv.pszVal)+23))
									sprintf(msg, "SET %s STATUS INPROGRESS", dbv.pszVal);
							}
							break;
						case IDC_HANGUP:
							if (msg=malloc(strlen(dbv.pszVal)+22))
								sprintf(msg, "SET %s STATUS FINISHED", dbv.pszVal);
							break;
					}
					
					DBFreeVariant(&dbv);
					DBFreeVariant(&dbv2);
					if (msg) {
						SkypeSend(msg);
						free(msg);
					}
					DestroyWindow(hwndDlg);
					break;
				}
			}
			break;
		case WM_DESTROY:
			Utils_SaveWindowPosition(hwndDlg, NULL, pszSkypeProtoName, "CALLSTATdlg");
			break;
	}
	return 0;
}


/* SkypeOutCallErrorCheck
 * 
 * Purpose: Checks, if an error has occured after call and
 *          if so, hangs up the call 
 *		    This procedure is a seperate thread to not block the core 
 *		    while waiting for "ERROR"
 * Params : szCallId - ID of the call
 */
void SkypeOutCallErrorCheck(char *szCallId) {
	if (testfor("ERROR", 500)) EndCallThread(szCallId);
}

/* SkypeOutCall
 * 
 * Purpose: Give a SkypeOut call to the given User in wParam
 *          or hangs up existing call
 *			The user's record is searched for Phone-number entries.
 *			If there is more than 1 entry, the Dial-Dialog is shown
 * Params : wParam - Handle to the User to be called
 *					 If NULL, the dial-dialog is shown
 * Returns: 0 - Success
 *		   -1 - Failure
 */
int SkypeOutCall(WPARAM wParam, LPARAM lParam) {
	DBVARIANT dbv;
	char *msg;
	int res;

	if (wParam && !DBGetContactSetting((HANDLE)wParam, pszSkypeProtoName, "CallId", &dbv)) {
		msg=(char *)malloc(strlen(dbv.pszVal)+21);
		sprintf(msg, "SET %s STATUS FINISHED", dbv.pszVal);
		res=SkypeSend(msg);
		pthread_create(SkypeOutCallErrorCheck, strdup(dbv.pszVal));
		DBFreeVariant(&dbv);
		free(msg);
	} else if (!CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_DIAL), NULL, DialDlgProc, (LPARAM)wParam)) return -1;
	return res;
}

/* SkypeHoldCall
 * 
 * Purpose: Put the call to the User given in wParam on Hold or Resumes it
 * Params : wParam - Handle to the User 
 * Returns: 0 - Success
 *		   -1 - Failure
 */
int SkypeHoldCall(WPARAM wParam, LPARAM lParam) {
	DBVARIANT dbv;
	char *msg;
	int retval;

	LOG("SkypeHoldCall", "started");
	if (!wParam || DBGetContactSetting((HANDLE)wParam, pszSkypeProtoName, "CallId", &dbv))
		return -1;
	
	if (!(msg=(char *)msg=malloc(strlen(dbv.pszVal)+23))) {
		DBFreeVariant(&dbv);
		return -1;
	}
	sprintf(msg, "SET %s STATUS ", dbv.pszVal);
	DBFreeVariant(&dbv);
	if (DBGetContactSettingByte((HANDLE)wParam, pszSkypeProtoName, "OnHold", 0)) strcat (msg, "INPROGRESS");
	else strcat(msg, "ONHOLD");

	retval=SkypeSend(msg);
	free(msg);
	return retval;
}

/* SkypeAnswerCall
 * 
 * Purpose: Answer a Skype-call when a user double-clicks on
 *			The incoming-call-Symbol. Works for both, Skype and SkypeOut-calls
 * Params : wParam - Not used
 *			lParam - CLISTEVENT*
 * Returns: 0 - Success
 *		   -1 - Failure
 */
int SkypeAnswerCall(WPARAM wParam, LPARAM lParam) {

	LOG("SkypeAnswerCall", "started");
	CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_CALLSTAT), NULL, CallstatDlgProc, (LPARAM)((CLISTEVENT*)lParam)->hContact);
	return 0;
}

/* SkypeSendFile
 * 
 * Purpose: Opens the Skype-dialog to send a file
 * Params : wParam - Handle to the User 
 *          lParam - Not used
 * Returns: 0 - Success
 *		   -1 - Failure
 */
int SkypeSendFile(WPARAM wParam, LPARAM lParam) {
	DBVARIANT dbv;
	int retval;

	if (!wParam || DBGetContactSetting((HANDLE)wParam, pszSkypeProtoName, SKYPE_NAME, &dbv))
		return -1;
	retval=SkypeSend("OPEN FILETRANSFER %s", dbv.pszVal);
	DBFreeVariant(&dbv);
	return retval;
}


/* SkypeAdduserDlg
 * 
 * Purpose: Show Skype's Add user Dialog
 */
int SkypeAdduserDlg(WPARAM wParam, LPARAM lParam) {
	SkypeSend("OPEN ADDAFRIEND");
	return 0;
}

/* SkypeFlush
 * 
 * Purpose: Flush the Skype Message-List
 */
void SkypeFlush(void) {
	char *ptr;

	while ((ptr=SkypeRcv(NULL, 0))!=NULL) free(ptr);
}

/* SkypeStatusToMiranda
 * 
 * Purpose: Converts the specified Skype-Status mode to the corresponding Miranda-Status mode
 * Params : s - Skype Status
 * Returns: The correct Status
 *		    0 - Nothing found
 */
int SkypeStatusToMiranda(char *s) {
	int i;
	if (!strcmp("SKYPEOUT", s)) return DBGetContactSettingDword(NULL, pszSkypeProtoName, "SkypeOutStatusMode", ID_STATUS_ONTHEPHONE);
	for(i=0; status_codes[i].szStat; i++)
		if (!strcmp(status_codes[i].szStat, s))
		return status_codes[i].id;
	return 0;
}

/* MirandaStatusToSkype
 * 
 * Purpose: Converts the specified Miranda-Status mode to the corresponding Skype-Status mode
 * Params : id - Miranda Status
 * Returns: The correct Status
 *		    NULL - Nothing found
 */
char *MirandaStatusToSkype(int id) {
	int i;
	for(i=0; status_codes[i].szStat; i++)
		if (status_codes[i].id==id)
			return status_codes[i].szStat;
	return NULL;
}

/* GetSkypeErrorMsg
 * 
 * Purpose: Get a human-readable Error-Message for the supplied Skype Error-Message
 * Params : str - Skype Error-Message string
 * Returns: Human-readable Error Message or NULL, if nothing was found
 * Warning: Don't forget to free() return value
 */
char *GetSkypeErrorMsg(char *str) {
	char *pos, *reason, *msg;

	if (!strncmp(str, "ERROR", 5)) {
		reason=strdup(str);
		return reason;
	}
	if ((pos=strstr(str, "FAILURE")) ) {
		switch(atoi(pos+14)) {
			case MISC_ERROR: msg=strdup("Misc. Error"); break;
			case USER_NOT_FOUND: msg=strdup("User does not exist, check username"); break;
			case USER_NOT_ONLINE: msg=strdup("Trying to send IM to an user, who is not online"); break;
			case USER_BLOCKED: msg=strdup("IM blocked by recipient"); break;
			case TYPE_UNSUPPORTED: msg=strdup("Type unsupported"); break;
			case SENDER_NOT_FRIEND: msg=strdup("Sending IM message to user, who has not added you to friendslist and has chosen 'only people in my friendslist can start IM'"); break;
			case SENDER_NOT_AUTHORIZED: msg=strdup("Sending IM message to user, who has not authorized you and has chosen 'only people whom I have authorized can start IM'"); break;
			default: msg=strdup("Unknown error");
		}
		reason=(char *)malloc(strlen(pos)+strlen(msg)+3);
		strcpy(reason, pos);
		strcat(reason, ": ");
		strcat(reason, msg);
		free(msg);
		return reason;
	}
	return NULL;
}

/* testfor
 * 
 * Purpose: Wait, until the given Message-Fragment is received from Skype within
 *			the given amount of time
 * Params : see SkypeRcv
 * Returns: TRUE - Message was received within the given amount of time
 *			FALSE- nope, sorry
 */
BOOL testfor(char *what, DWORD maxwait) {
	char *res;

	if ((res=SkypeRcv(what, maxwait))==NULL) return FALSE;
	free(res);
	return TRUE;
}

char SendSkypeproxyCommand(char command) {
	int length=0;
	char reply=0;

	if (send(ClientSocket, (char *)&length, sizeof(length), 0)==SOCKET_ERROR ||
		send(ClientSocket, (char *)&command, sizeof(command), 0)==SOCKET_ERROR ||
		recv(ClientSocket, (char *)&reply, sizeof(reply), 0)==SOCKET_ERROR)
			return -1;
	else return reply;
}

/* ConnectToSkypeAPI
 * 
 * Purpose: Establish a connection to the Skype API
 * Params : path - Path to the Skype application
 * Returns: 0 - Connecting succeeded
 *		   -1 - Something went wrong
 */
int ConnectToSkypeAPI(char *path) {
	BOOL SkypeLaunched=FALSE;
	int counter=0, i, j, maxattempts=DBGetContactSettingWord(NULL, pszSkypeProtoName, "ConnectionAttempts", 5);
	char *args[5];
	char *SkypeOptions[]={"/notray", "/nosplash", "/minimized"};
	const int SkypeDefaults[]={0, 1, 1};

	AttachStatus=-1;
	if (UseSockets) {
		SOCKADDR_IN service;
		DBVARIANT dbv;
		long inet;
		struct hostent *hp;

		LOG("ConnectToSkypeAPI", "Connecting to Skype2socket socket...");
		if ((ClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))==INVALID_SOCKET)
			return -1;

		if (!DBGetContactSetting(NULL, pszSkypeProtoName, "Host", &dbv)) {
			if ((inet=inet_addr(dbv.pszVal))==-1) {
				if (hp=gethostbyname(dbv.pszVal))
					memcpy(&inet, hp->h_addr, sizeof(inet));
				else {
					OUTPUT("Cannot resolve host!");
					DBFreeVariant(&dbv);
					return -1;
				}
			}
			DBFreeVariant(&dbv);
		} else {
			OUTPUT("Cannot find valid host to connect to.");
			return -1;
		}

		service.sin_family = AF_INET;
		service.sin_addr.s_addr = inet;
		service.sin_port = htons((unsigned short)DBGetContactSettingWord(NULL, pszSkypeProtoName, "Port", 1401));
	
		if ( connect( ClientSocket, (SOCKADDR*) &service, sizeof(service) ) == SOCKET_ERROR)
			return -1;

		if (DBGetContactSettingByte(NULL, pszSkypeProtoName, "RequiresPassword", 0) && 
			!DBGetContactSetting(NULL, pszSkypeProtoName, "Password", &dbv)) {
				char reply=0;

				CallService(MS_DB_CRYPT_DECODESTRING, strlen(dbv.pszVal)+1, (LPARAM)dbv.pszVal);
				if ((reply=SendSkypeproxyCommand(AUTHENTICATE))==-1) {
						DBFreeVariant(&dbv);
						return -1;
				}
				if (!reply) {
					OUTPUT("Authentication is not supported/needed for this Skypeproxy server. It will be disabled.");
					DBWriteContactSettingByte(NULL, pszSkypeProtoName, "RequiresPassword", 0);
				} else {
					unsigned int length=strlen(dbv.pszVal);
					if (send(ClientSocket, (char *)&length, sizeof(length), 0)==SOCKET_ERROR ||
						send(ClientSocket, dbv.pszVal, length, 0)==SOCKET_ERROR ||
						recv(ClientSocket, (char *)&reply, sizeof(reply), 0)==SOCKET_ERROR) {
							DBFreeVariant(&dbv);
							return -1;
					}
					if (!reply) {
						OUTPUT("Authentication failed for this server, connection was not successful. Verify that your password is correct!");
						DBFreeVariant(&dbv);
						return -1;
					}
				}
				DBFreeVariant(&dbv);
		} else {
			char reply=0;

			if ((reply=SendSkypeproxyCommand(CAPABILITIES))==-1) return -1;
			if (reply&USE_AUTHENTICATION) {
				OUTPUT("The server you specified requires authentication, but you have not supplied a password for it. Check the Skype plugin settings and try again.");
				return -1;
			}
		}


		if (!rcvThreadRunning)
			if(_beginthread(rcvThread, 0, NULL)==-1)
				return -1;
		AttachStatus=SKYPECONTROLAPI_ATTACH_SUCCESS;
		return 0;
	}

	do {
		/*	To initiate communication, Client should broadcast windows message
			('SkypeControlAPIDiscover') to all windows in the system, specifying its own
			window handle in wParam parameter.
		 */
		LOG("ConnectToSkypeAPI", "Sending discover message..");
		SendMessageTimeout(HWND_BROADCAST, ControlAPIDiscover, (WPARAM)hWnd, 0, SMTO_ABORTIFHUNG, 3000, NULL);
		LOG("ConnectToSkypeAPI", "Discover message sent, waiting for Skype to become ready..");

		/*	In response, Skype responds with
			message 'SkypeControlAPIAttach' to the handle specified, and indicates
			connection status
			SkypeReady is set if there is an answer by Skype other than API_AVAILABLE.
			If there is no answer after 3 seconds, launch Skype as it's propably
			not running.
		*/
		if (WaitForSingleObject(SkypeReady, 3000)==WAIT_TIMEOUT &&
			AttachStatus!=SKYPECONTROLAPI_ATTACH_PENDING_AUTHORIZATION) {
			if (hWnd==NULL) {
				LOG("ConnectToSkypeAPI", "hWnd of SkypeDispatchWindow not yet set..");
				continue;
			}
			if (!SkypeLaunched && path) {
				if (DBGetContactSettingByte(NULL, pszSkypeProtoName, "StartSkype", 1)) {
					LOG("ConnectToSkypeAPI", "Starting Skype, as it's not running");
					args[0]=path;
					j=1;
					for (i=0; i<3; i++)
						if (DBGetContactSettingByte(NULL, pszSkypeProtoName, SkypeOptions[i]+1, SkypeDefaults[i])) {
							args[j]=SkypeOptions[i];
							LOG("Using Skype parameter: ", args[j]);
							j++;
						}
					args[j]=NULL;
					_spawnv(_P_NOWAIT, path, args);
				}
				ResetEvent(SkypeReady);
				SkypeLaunched=TRUE;
				LOG("ConnectToSkypeAPI", "Skype process started.");
				// Skype launching iniciated, keep sending Discover messages until it responds.
				continue;
			} else {
				LOG("ConnectToSkypeAPI", "Check if Skype was launchable..");
				if (DBGetContactSettingByte(NULL, pszSkypeProtoName, "StartSkype", 1) && !path) return -1;
				LOGL("Trying to attach: #", counter);
				counter++;
				if (counter==maxattempts) {
					OUTPUT("ERROR: Skype not running / too old / working!");
					return -1;
				}
			}
		}
		LOGL("Attachstatus", AttachStatus);
	} while (AttachStatus==SKYPECONTROLAPI_ATTACH_NOT_AVAILABLE ||
			 AttachStatus==SKYPECONTROLAPI_ATTACH_API_AVAILABLE || 
			 AttachStatus==-1);
	
	while (AttachStatus==SKYPECONTROLAPI_ATTACH_PENDING_AUTHORIZATION) Sleep(1000);
	LOGL("Attachstatus", AttachStatus);
	if (AttachStatus!=SKYPECONTROLAPI_ATTACH_SUCCESS) {
		switch(AttachStatus) {
			case SKYPECONTROLAPI_ATTACH_REFUSED:
				OUTPUT("Skype refused the connection :(");
				break;
			case SKYPECONTROLAPI_ATTACH_NOT_AVAILABLE:
				OUTPUT("The Skype API is not available");
				break;
			default:
				LOGL("ERROR: AttachStatus:", AttachStatus);
				OUTPUT("Wheee, Skype won't let me use the API. :(");
		}
		return -1;
	}
	
	return 0;
}