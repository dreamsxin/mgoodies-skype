#pragma once

#include "Interface.h"
#include "BTree.h"
#include "FileBTree.h"
#include "BlockManager.h"
#include "IterationHeap.h"
#include "Entities.h"
#include "Settings.h"
#include "Hash.h"
#include "EncryptionManager.h"
#include "sigslot.h"

#include <hash_map>
#include <hash_set>
#include <queue>
#include <time.h>
#include <windows.h>

#pragma pack(push, 1)  // push current alignment to stack, set alignment to 1 byte boundary

/**
	\brief Key Type of the EventsBTree
	
	The Key consists of a timestamp, seconds elapsed since 1.1.1970
	and an Index, which makes it possible to store multiple events with the same timestamp
**/
typedef struct TEventKey {
	uint32_t        TimeStamp; /// timestamp at which the event occoured
	uint32_t        Index;     /// index counted globally
	TDBTEventHandle Event;

	bool operator <  (const TEventKey & Other) const;
	//bool operator <= (const TEventKey & Other);
	bool operator == (const TEventKey & Other) const;
	//bool operator >= (const TEventKey & Other);
	bool operator >  (const TEventKey & Other) const;
} TEventKey;

/**
	\brief Key Type of the EventLinkBTree
**/
typedef struct TEventLinkKey {
	TDBTEventHandle   Event;    /// handle to the event
	TDBTEntityHandle Entity;  /// handle to the Entity which includes this event

	bool operator <  (const TEventLinkKey & Other) const;
	//bool operator <= (const TEventKey & Other);
	bool operator == (const TEventLinkKey & Other) const;
	//bool operator >= (const TEventKey & Other);
	bool operator >  (const TEventLinkKey & Other) const;
} TEventLinkKey;

/**
	\brief The data of an Event

	A event's data is variable length. The data is a TDBTEvent-structure followed by varaible length data.
	- fixed data
	- blob data (mostly UTF8 message body)
**/
typedef struct TEvent {
	uint32_t Flags;				       /// Flags
	uint32_t TimeStamp;          /// Timestamp of the event (seconds elapsed since 1.1.1970) used as key element
	uint32_t Index;              /// index counter to seperate events with the same timestamp
	uint32_t Type;               /// Eventtype
	union {
		TDBTEntityHandle Entity;  /// hEntity which owns this event
		uint32_t ReferenceCount;   /// Reference Count, if event was hardlinked
	};
	uint32_t DataLength;         /// Length of the stored data in bytes

	uint8_t Reserved[8];            /// reserved storage
} TEvent;

#pragma pack(pop)



static const uint32_t cEventSignature = 0x365A7E92;
static const uint16_t cEventNodeSignature = 0x195C;
static const uint16_t cEventLinkNodeSignature = 0xC16A;



/**
	\brief Manages the Events Index in the Database
**/
class CEventsTree : public CFileBTree<TEventKey, 16>
{
private:
	TDBTEntityHandle m_Entity;

public:
	CEventsTree(CBlockManager & BlockManager, TNodeRef RootNode, TDBTEntityHandle Entity);
	~CEventsTree();

	TDBTEntityHandle getEntity();
	void setEntity(TDBTEntityHandle NewEntity);
};

/**
	\brief Manages the Virtual Events Index
	Sorry for duplicating code...
**/
class CVirtualEventsTree : public CBTree<TEventKey, 16>
{
private:
	TDBTEntityHandle m_Entity;

public:
	CVirtualEventsTree(TDBTEntityHandle Entity);
	~CVirtualEventsTree();

	TDBTEntityHandle getEntity();
	void setEntity(TDBTEntityHandle NewEntity);
};


class CEventsTypeManager 
{
public:
	CEventsTypeManager(CEntities & Entities, CSettings & Settings);
	~CEventsTypeManager();

	uint32_t MakeGlobalID(char* Module, uint32_t EventType);
	bool GetType(uint32_t GlobalID, char * & Module, uint32_t & EventType);
	uint32_t EnsureIDExists(char* Module, uint32_t EventType);

private:
	typedef struct TEventType {
		char *   ModuleName;
		uint32_t EventType;
	} TEventType, *PEventType;
	typedef stdext::hash_map<uint32_t, PEventType> TTypeMap;

	CEntities & m_Entities;
	CSettings & m_Settings;
	TTypeMap m_Map;

};


class CEventLinks : public CFileBTree<TEventLinkKey, 8>
{
public:
	CEventLinks(CBlockManager & BlockManager, TNodeRef RootNode);
	~CEventLinks();

private:


};

class CEvents : public sigslot::has_slots<>
{
public:

	CEvents(
		CBlockManager & BlockManager, 
		CEncryptionManager & EncryptionManager,
		CEventLinks::TNodeRef LinkRootNode, 
		CMultiReadExclusiveWriteSynchronizer & Synchronize,
		CEntities & Entities, 
		CSettings & Settings,
		uint32_t IndexCounter
		);
	~CEvents();

	CEventLinks::TOnRootChanged & sigLinkRootChanged();
	
	typedef sigslot::signal2<CEvents *, uint32_t> TOnIndexCounterChanged;
	TOnIndexCounterChanged & _sigIndexCounterChanged();


	//compatibility
	TDBTEventHandle compFirstEvent(TDBTEntityHandle hEntity);
	TDBTEventHandle compFirstUnreadEvent(TDBTEntityHandle hEntity);
	TDBTEventHandle compLastEvent(TDBTEntityHandle hEntity);
	TDBTEventHandle compNextEvent(TDBTEventHandle hEvent);
	TDBTEventHandle compPrevEvent(TDBTEventHandle hEvent);

	//services
	unsigned int GetBlobSize(TDBTEventHandle hEvent);
	unsigned int Get(TDBTEventHandle hEvent, TDBTEvent & Event);
	unsigned int GetCount(TDBTEntityHandle hEntity);
	unsigned int Delete(TDBTEntityHandle hEntity, TDBTEventHandle hEvent);
	TDBTEventHandle Add(TDBTEntityHandle hEntity, TDBTEvent & Event);
	unsigned int MarkRead(TDBTEntityHandle hEntity, TDBTEventHandle hEvent);
	unsigned int WriteToDisk(TDBTEntityHandle hEntity, TDBTEventHandle hEvent);
	unsigned int HardLink(TDBTEventHardLink & HardLink);

	TDBTEntityHandle getEntity(TDBTEventHandle hEvent);

	TDBTEventIterationHandle IterationInit(TDBTEventIterFilter & Filter);
	TDBTEventHandle IterationNext(TDBTEventIterationHandle Iteration);
	unsigned int IterationClose(TDBTEventIterationHandle Iteration);


private:
	typedef CBTree<TEventKey, 16> TEventBase;
	typedef stdext::hash_map<TDBTEntityHandle, CEventsTree*> TEventsTreeMap;
	typedef stdext::hash_map<TDBTEntityHandle, CVirtualEventsTree*> TVirtualEventsTreeMap;
	typedef stdext::hash_map<TDBTEntityHandle, uint32_t> TVirtualEventsCountMap;
	typedef CIterationHeap<TEventBase::iterator> TEventsHeap;
	
	typedef stdext::hash_set<TDBTEntityHandle> TVirtualOwnerSet;
	typedef stdext::hash_map<TDBTEventHandle, TVirtualOwnerSet*> TVirtualOwnerMap;

	CMultiReadExclusiveWriteSynchronizer & m_Sync;
	CBlockManager & m_BlockManager;
	CEncryptionManager & m_EncryptionManager;

	CEntities & m_Entities;
	CEventsTypeManager m_Types;
	CEventLinks m_Links;

	TEventsTreeMap m_EventsMap;
	TVirtualEventsTreeMap m_VirtualEventsMap;
	TVirtualOwnerMap m_VirtualOwnerMap;
	TVirtualEventsCountMap m_VirtualCountMap;

	uint32_t m_Counter;
	TOnIndexCounterChanged m_sigIndexCounterChanged;

	typedef struct TEventIteration {
		TDBTEventIterFilter Filter;
		TEventsHeap * Heap;
		TDBTEventHandle LastEvent;
	} TEventIteration, *PEventIteration;

	void onRootChanged(void* EventsTree, CEventsTree::TNodeRef NewRoot);

	void onDeleteEventCallback(void * Tree, const TEventKey & Key, uint32_t Param);
	void onDeleteVirtualEventCallback(void * Tree, const TEventKey & Key, uint32_t Param);
	void onDeleteEvents(CEntities * Entities, TDBTEntityHandle hEntity);
	void onTransferEvents(CEntities * Entities, TDBTEntityHandle Source, TDBTEntityHandle Dest);

	CEventsTree * getEventsTree(TDBTEntityHandle hEntity);
	CVirtualEventsTree * getVirtualEventsTree(TDBTEntityHandle hEntity);
	uint32_t adjustVirtualEventCount(TDBTEntityHandle hEntity, int32_t Adjust);
};
