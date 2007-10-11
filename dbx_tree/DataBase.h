#pragma once

#include "MREWSync.h"

#include "Events.h"
#include "Settings.h"
#include "Virtuals.h"
#include "Entries.h"

#include "FileAccess.h"
#include "MappedMemory.h"
#include "DirectAccess.h"

#include "sigslot.h"


static const char cSettingsSignature[20] = "Miranda IM Settings";
static const char cPrivateSignature[20]  = "Miranda IM DataTree";
static const unsigned int cDBVersion = 0x00000001;

#pragma pack(push)  /* push current alignment to stack */
#pragma pack(1)     /* set alignment to 1 byte boundary */

typedef struct TSettingsHeader {
	char Signature[20];       /// signature must be cSettingsHeader
	unsigned int Version;     /// internal DB version cDataBaseVersion
	unsigned int Settings;    /// Offset to the SettingsBTree RootNode
	unsigned int FileSize;    /// Offset to the last used byte + 1
	unsigned int WastedBytes; /// Lost bytes between the data
	char Reserved[92];        /// reserved storage
} TSettingsHeader;



typedef struct TPrivateHeader {
	char Signature[20];       /// signature must be CDataHeader
	unsigned int Version;     /// internal DB version cDataBaseVersion
	unsigned int RootEntry;   /// Offset to the Root CList Entry
	unsigned int Entries;     /// Offset to the EntryBTree RootNode
	unsigned int Virtuals;    /// Offset to the VirtualsBTree RootNode
	unsigned int FileSize;    /// Offset to the last used byte + 1
	unsigned int WastedBytes; /// Lost bytes between the data
	unsigned int EventIndex;  /// global counter for event index
	char Reserved[80];        /// reserved storage
} TPrivateHeader;

#pragma pack(pop)


class CDataBase : public sigslot::has_slots<>
{
private:
	char* m_SettingsFN;
	char* m_PrivateFN;
	bool m_Opened;

	CFileAccess *m_SettingsFile;
	CFileAccess *m_PrivateFile;

	TSettingsHeader m_SettingsHeader;
	TPrivateHeader m_PrivateHeader;

	void onSettingsRootChanged(void* Settings, unsigned int NewRoot);
	void onVirtualsRootChanged(void* Virtuals, unsigned int NewRoot);
	void onEntriesRootChanged(void* Entries, unsigned int NewRoot);
protected:
	CMultiReadExclusiveWriteSynchronizer m_Sync;

	CEntries *m_Entries;

	CVirtuals *m_Virtuals;
	CSettings *m_Settings;


public:
	CDataBase(const char* FileName);
	virtual ~CDataBase();

	int CreateDB();
	int CheckDB();
	int OpenDB();
};


extern CDataBase *gDataBase;