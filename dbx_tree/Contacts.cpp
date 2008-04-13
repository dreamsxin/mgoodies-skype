#include "Contacts.h"

inline bool TVirtualKey::operator <  (const TVirtualKey & Other) const
{
	if (RealContact != Other.RealContact) return RealContact < Other.RealContact;
	if (Virtual != Other.Virtual) return Virtual < Other.Virtual;
	return false;
}

inline bool TVirtualKey::operator == (const TVirtualKey & Other) const
{
	return (RealContact == Other.RealContact) && (Virtual == Other.Virtual);
}

inline bool TVirtualKey::operator >  (const TVirtualKey & Other) const
{	
	if (RealContact != Other.RealContact) return RealContact > Other.RealContact;
	if (Virtual != Other.Virtual) return Virtual > Other.Virtual;
	return false;
}


inline bool TContactKey::operator <  (const TContactKey & Other) const
{
	if (Level != Other.Level) return Level < Other.Level;
	if (Parent != Other.Parent) return Parent < Other.Parent;
	if (Contact != Other.Contact) return Contact < Other.Contact;
	return false;
}

inline bool TContactKey::operator == (const TContactKey & Other) const
{
	return (Level == Other.Level) && (Parent == Other.Parent) && (Contact == Other.Contact);
}

inline bool TContactKey::operator >  (const TContactKey & Other) const
{	
	if (Level != Other.Level) return Level > Other.Level;
	if (Parent != Other.Parent) return Parent > Other.Parent;
	if (Contact != Other.Contact) return Contact > Other.Contact;
	return false;
}






CVirtuals::CVirtuals(CBlockManager & BlockManager, CMultiReadExclusiveWriteSynchronizer & Synchronize, TNodeRef RootNode)
: CFileBTree(BlockManager, RootNode, cVirtualNodeSignature),
	m_Sync(Synchronize)
{

}

CVirtuals::~CVirtuals()
{

}

TDBContactHandle CVirtuals::_DeleteRealContact(TDBContactHandle hRealContact)
{
	TDBContactHandle result;
	TVirtualKey key;
	TContact Contact;
	bool copies = false;
	uint32_t sig = cContactSignature;

	key.RealContact = hRealContact;
	key.Virtual = 0;

	iterator i = LowerBound(key);
	result = i.Key().Virtual;
	i.setManaged();
	Delete(i);

	while ((i) && (i.Key().RealContact == hRealContact))
	{
		key = i.Key();
		Delete(i);

		key.RealContact = result;		
		Insert(key, TEmpty());		

		Contact.VParent = result;
		m_BlockManager.WritePart(key.Virtual, &Contact.VParent, offsetof(TContact, VParent), sizeof(TDBContactHandle));
	
		copies = true;
	}

	m_BlockManager.ReadPart(result, &Contact.Flags, offsetof(TContact, Flags), sizeof(uint32_t), sig);
	Contact.Flags = Contact.Flags & ~(DB_CF_HasVirtuals | DB_CF_IsVirtual);
	if (copies)
		Contact.Flags |= DB_CF_HasVirtuals;

	m_BlockManager.WritePart(result, &Contact.Flags, offsetof(TContact, Flags), sizeof(uint32_t));

	return result;
}

bool CVirtuals::_InsertVirtual(TDBContactHandle hRealContact, TDBContactHandle hVirtual)
{
	TVirtualKey key;
	key.RealContact = hRealContact;
	key.Virtual = hVirtual;

	Insert(key, TEmpty());

	return true;
}
void CVirtuals::_DeleteVirtual(TDBContactHandle hRealContact, TDBContactHandle hVirtual)
{
	TVirtualKey key;
	key.RealContact = hRealContact;
	key.Virtual = hVirtual;

	Delete(key);
}
TDBContactHandle CVirtuals::getParent(TDBContactHandle hVirtual)
{
	TContact Contact;
	void* p = &Contact;
	uint32_t size = sizeof(Contact);
	uint32_t sig = cContactSignature;

	m_Sync.BeginRead();
	if (!m_BlockManager.ReadBlock(hVirtual, p, size, sig) || 
	   ((Contact.Flags & DB_CF_IsVirtual) == 0))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}

	m_Sync.EndRead();	
	return Contact.VParent;
}
TDBContactHandle CVirtuals::getFirst(TDBContactHandle hRealContact)
{
	TContact Contact;
	void* p = &Contact;
	uint32_t size = sizeof(Contact);
	uint32_t sig = cContactSignature;

	m_Sync.BeginRead();
	

	if (!m_BlockManager.ReadBlock(hRealContact, p, size, sig) || 
	   ((Contact.Flags & DB_CF_HasVirtuals) == 0))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}
	
	TVirtualKey key;
	key.RealContact = hRealContact;
	key.Virtual = 0;

	iterator i = LowerBound(key);

	if ((i) && (i.Key().RealContact == hRealContact))
		key.Virtual = i.Key().Virtual;
	else
		key.Virtual = 0;

	m_Sync.EndRead();

	return key.Virtual;
}
TDBContactHandle CVirtuals::getNext(TDBContactHandle hVirtual)
{
	TContact Contact;
	void* p = &Contact;
	uint32_t size = sizeof(Contact);
	uint32_t sig = cContactSignature;

	m_Sync.BeginRead();
	
	if (!m_BlockManager.ReadBlock(hVirtual, p, size, sig) || 
	   ((Contact.Flags & DB_CF_IsVirtual) == 0))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}
	
	TVirtualKey key;
	key.RealContact = Contact.VParent;
	key.Virtual = hVirtual + 1;

	iterator i = LowerBound(key);
	
	if ((i) && (i.Key().RealContact == Contact.VParent))
		key.Virtual = i.Key().Virtual;
	else
		key.Virtual = 0;

	m_Sync.EndRead();

	return key.Virtual;
}




///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

CContacts::CContacts(CBlockManager & BlockManager, CMultiReadExclusiveWriteSynchronizer & Synchronize, TDBContactHandle RootContact, TNodeRef ContactRoot, CVirtuals::TNodeRef VirtualRoot)
: CFileBTree(BlockManager, ContactRoot, cContactNodeSignature),
	m_Sync(Synchronize),
	m_Virtuals(BlockManager, Synchronize, VirtualRoot),
	m_Iterations()
{
	if (RootContact == 0)
		m_RootContact = CreateRootContact();
	else
		m_RootContact = RootContact;
	
}

CContacts::~CContacts()
{
	m_Sync.BeginWrite();
	for (unsigned int i = 0; i < m_Iterations.size(); ++i)
	{
		if (m_Iterations[i])
			IterationClose(i + 1);
	}

	m_Sync.EndWrite();
}

TDBContactHandle CContacts::CreateRootContact()
{
	TContact Contact = {0};
	TContactKey key = {0};

	Contact.Flags = DB_CF_IsGroup | DB_CF_IsRoot;
	key.Contact = m_BlockManager.CreateBlock(sizeof(Contact), cContactSignature);
	m_BlockManager.WriteBlock(key.Contact, &Contact, sizeof(Contact), cContactSignature);
	Insert(key, TEmpty());
	return key.Contact;
}


CVirtuals::TOnRootChanged & CContacts::sigVirtualRootChanged()
{
	return m_Virtuals.sigRootChanged();
}


uint32_t CContacts::_getSettingsRoot(TDBContactHandle hContact)
{
	/*CSettingsTree::TNodeRef*/
	uint32_t set;
	uint32_t sig = cContactSignature;

	if (!m_BlockManager.ReadPart(hContact, &set, offsetof(TContact, Settings), sizeof(set), sig))
		return DB_INVALIDPARAM;

	return set;
}
bool CContacts::_setSettingsRoot(TDBContactHandle hContact, /*CSettingsTree::TNodeRef*/ uint32_t NewRoot)
{
	uint32_t sig = cContactSignature;

	if (!m_BlockManager.WritePartCheck(hContact, &NewRoot, offsetof(TContact, Settings), sizeof(NewRoot), sig))
		return false;
	
	return true;
}

uint32_t CContacts::_getEventsRoot(TDBContactHandle hContact)
{
	/*CEventsTree::TNodeRef*/
	uint32_t ev;
	uint32_t sig = cContactSignature;

	if (!m_BlockManager.ReadPart(hContact, &ev, offsetof(TContact, Events), sizeof(ev), sig))
		return DB_INVALIDPARAM;

	return ev;
}
bool CContacts::_setEventsRoot(TDBContactHandle hContact, /*CEventsTree::TNodeRef*/ uint32_t NewRoot)
{
	uint32_t sig = cContactSignature;

	if (!m_BlockManager.WritePartCheck(hContact, &NewRoot, offsetof(TContact, Events), sizeof(NewRoot), sig))
		return false;
	
	return true;
}


CVirtuals & CContacts::_getVirtuals()
{
	return m_Virtuals;
}

TDBContactHandle CContacts::getRootContact()
{
	return m_RootContact;
}

TDBContactHandle CContacts::getParent(TDBContactHandle hContact)
{
	TDBContactHandle par;
	uint32_t sig = cContactSignature;

	m_Sync.BeginRead();	
	if (!m_BlockManager.ReadPart(hContact, &par, offsetof(TContact, ParentContact), sizeof(par), sig))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}

	m_Sync.EndRead();	
	return par;
}
TDBContactHandle CContacts::setParent(TDBContactHandle hContact, TDBContactHandle hParent)
{
	TContact Contact;
	void* pContact = &Contact;
	uint32_t size = sizeof(TContact);
	uint32_t sig = cContactSignature;
	uint16_t cn, co;
	uint32_t fn, fo;
	uint16_t l;
	
	m_Sync.BeginWrite();
	if (!m_BlockManager.ReadBlock(hContact, pContact, size, sig) ||
		  !m_BlockManager.ReadPart(hParent, &cn, offsetof(TContact,ChildCount), sizeof(cn), sig) ||
			!m_BlockManager.ReadPart(Contact.ParentContact, &co, offsetof(TContact, ChildCount), sizeof(co), sig) ||
			!m_BlockManager.ReadPart(hParent, &l, offsetof(TContact, Level), sizeof(l), sig))
	{
		m_Sync.EndWrite();
		return DB_INVALIDPARAM;
	}

	// update parents
	--co;
	++cn;

	m_BlockManager.WritePart(Contact.ParentContact, &co, offsetof(TContact, ChildCount),sizeof(co));
	if (co == 0)
	{
		m_BlockManager.ReadPart(Contact.ParentContact, &fo, offsetof(TContact, Flags), sizeof(fo), sig);
		fo = fo & ~DB_CF_HasChildren;
		m_BlockManager.WritePart(Contact.ParentContact, &fo, offsetof(TContact, Flags), sizeof(fo));
	}
	
	m_BlockManager.WritePart(hParent, &cn, offsetof(TContact, ChildCount), sizeof(cn));
	if (cn == 1)
	{
		m_BlockManager.ReadPart(hParent, &fn, offsetof(TContact, Flags), sizeof(fn), sig);
		fn = fn | DB_CF_HasChildren;
		m_BlockManager.WritePart(hParent, &fn, offsetof(TContact, Flags), sizeof(fn));
	}	
	
	// update rest

	TContactKey key;
	int dif = l - Contact.Level + 1;

	if (dif == 0) // no level difference, update only moved contact
	{
		key.Contact = hContact;
		key.Level = Contact.Level;
		key.Parent = Contact.ParentContact;
		Delete(key);
		key.Parent = hParent;
		Insert(key, TEmpty());
		m_BlockManager.WritePart(hContact, &key.Parent, offsetof(TContact, ParentContact), sizeof(key.Parent));
		
	} else {
		TDBContactIterFilter filter = {0};
		filter.cbSize = sizeof(filter);
		filter.Options = DB_CIFO_OSC_AC | DB_CIFO_OC_AC;

		TDBContactIterationHandle iter = IterationInit(filter, hContact);

		key.Contact = IterationNext(iter);

		while ((key.Contact != 0) && (key.Contact != DB_INVALIDPARAM))
		{
			if (m_BlockManager.ReadPart(key.Contact, &key.Parent, offsetof(TContact, ParentContact), sizeof(key.Parent), sig) &&
					m_BlockManager.ReadPart(key.Contact, &key.Level, offsetof(TContact, Level), sizeof(key.Level), sig))
			{
				Delete(key);

				if (key.Contact == hContact)
				{
					key.Parent = hParent;
					m_BlockManager.WritePart(key.Contact, &key.Parent, offsetof(TContact, ParentContact), sizeof(key.Parent));
				}
				
				key.Level = key.Level + dif;
				m_BlockManager.WritePart(key.Contact, &key.Level, offsetof(TContact, Level), sizeof(key.Level));
				
				Insert(key, TEmpty());
			}
			key.Contact = IterationNext(iter);
		}

		IterationClose(iter);
	}

	m_Sync.EndWrite();

	/// TODO raise event

	return Contact.ParentContact;
}

unsigned int CContacts::getChildCount(TDBContactHandle hContact)
{
	uint32_t c;
	uint32_t sig = cContactSignature;
	
	m_Sync.BeginRead();
	if (!m_BlockManager.ReadPart(hContact, &c, offsetof(TContact, ChildCount), sizeof(c), sig))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}
	
	return c;
}
TDBContactHandle CContacts::getFirstChild(TDBContactHandle hParent)
{
	uint16_t l;
	uint32_t f;
	uint32_t sig = cContactSignature;
	TDBContactHandle result;

	m_Sync.BeginRead();
	if (!m_BlockManager.ReadPart(hParent, &f, offsetof(TContact, Flags), sizeof(f), sig) ||
		  !m_BlockManager.ReadPart(hParent, &l, offsetof(TContact, Level), sizeof(l), sig))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}

	if ((f & DB_CF_HasChildren) == 0)
	{
		m_Sync.EndRead();
		return 0;
	}

	TContactKey key;

	key.Level = l + 1;
	key.Parent = hParent;
	key.Contact = 0;

	iterator it = LowerBound(key);
	
	if ((!it) || (it.Key().Parent != hParent))
	{
		m_Sync.EndRead();	
		return 0;
	}
	result = it.Key().Contact;
	m_Sync.EndRead();

	return result;
}
TDBContactHandle CContacts::getLastChild(TDBContactHandle hParent)
{
	uint16_t l;
	uint32_t f;
	uint32_t sig = cContactSignature;
	TDBContactHandle result;

	m_Sync.BeginRead();
	if (!m_BlockManager.ReadPart(hParent, &f, offsetof(TContact, Flags), sizeof(f), sig) ||
		  !m_BlockManager.ReadPart(hParent, &l, offsetof(TContact, Level), sizeof(l), sig))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}

	if ((f & DB_CF_HasChildren) == 0)
	{
		m_Sync.EndRead();
		return 0;
	}

	TContactKey key;

	key.Level = l + 1;
	key.Parent = hParent;
	key.Contact = 0xFFFFFFFF;

	iterator it = UpperBound(key);
	
	if ((!it) || (it.Key().Parent != hParent))
	{
		m_Sync.EndRead();	
		return 0;
	}

	result = it.Key().Contact;
	m_Sync.EndRead();

	return result;
}
TDBContactHandle CContacts::getNextSilbing(TDBContactHandle hContact)
{
	uint16_t l;
	uint32_t sig = cContactSignature;
	TDBContactHandle result, parent;

	m_Sync.BeginRead();
	if (!m_BlockManager.ReadPart(hContact, &l, offsetof(TContact, Level), sizeof(l), sig) ||
		  !m_BlockManager.ReadPart(hContact, &parent, offsetof(TContact, ParentContact), sizeof(parent), sig))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}

	TContactKey key;

	key.Level = l;
	key.Parent = parent;
	key.Contact = hContact + 1;

	iterator it = LowerBound(key);
	
	if ((!it) || (it.Key().Parent != parent))
	{
		m_Sync.EndRead();
		return 0;
	}
	result = it.Key().Contact;
	m_Sync.EndRead();

	return result;
}
TDBContactHandle CContacts::getPrevSilbing(TDBContactHandle hContact)
{
	uint16_t l;
	uint32_t sig = cContactSignature;
	TDBContactHandle result, parent;

	m_Sync.BeginRead();
	if (!m_BlockManager.ReadPart(hContact, &l, offsetof(TContact, Level), sizeof(l), sig) ||
		  !m_BlockManager.ReadPart(hContact, &parent, offsetof(TContact, ParentContact), sizeof(parent), sig))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}

	TContactKey key;

	key.Level = l;
	key.Parent = parent;
	key.Contact = hContact - 1;

	iterator it = UpperBound(key);
	
	if ((!it) || (it.Key().Parent != parent))
	{
		m_Sync.EndRead();
		return 0;
	}
	result = it.Key().Contact;
	m_Sync.EndRead();

	return result;
}

unsigned int CContacts::getFlags(TDBContactHandle hContact)
{
	uint32_t f;
	uint32_t sig = cContactSignature;

	m_Sync.BeginRead();
	if (!m_BlockManager.ReadPart(hContact, &f, offsetof(TContact, Flags), sizeof(f), sig))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}

	m_Sync.EndRead();
	return f;
}

TDBContactHandle CContacts::CreateContact(TDBContactHandle hParent, uint32_t Flags)
{
	TContact Contact = {0}, parent;
	void* pparent = &parent;
	uint32_t size = sizeof(Contact);
	uint32_t sig = cContactSignature;

	m_Sync.BeginWrite();
	if (!m_BlockManager.ReadBlock(hParent, pparent, size, sig))
	{
		m_Sync.EndWrite();
		return DB_INVALIDPARAM;
	}

	TDBContactHandle hContact = m_BlockManager.CreateBlock(sizeof(TContact), cContactSignature);
	TContactKey key;
	
	Contact.Level = parent.Level + 1;
	Contact.ParentContact = hParent;
	Contact.Flags = Flags;
	
	m_BlockManager.WriteBlock(hContact, &Contact, sizeof(Contact), cContactSignature);
	
	key.Level = Contact.Level;
	key.Parent = hParent;
	key.Contact = hContact;

	Insert(key, TEmpty());
	
	if (parent.ChildCount == 0)
	{
		parent.Flags = parent.Flags | DB_CF_HasChildren;
		m_BlockManager.WritePart(hParent, &parent.Flags, offsetof(TContact, Flags), sizeof(uint32_t));
	}
	++parent.ChildCount;
	m_BlockManager.WritePart(hParent, &parent.ChildCount, offsetof(TContact, ChildCount), sizeof(uint16_t));
	
	m_Sync.EndWrite();
	return hContact;
}

unsigned int CContacts::DeleteContact(TDBContactHandle hContact)
{
	TContact Contact;
	void* pContact = &Contact;
	uint32_t size = sizeof(Contact);
	uint32_t sig = cContactSignature;
	uint16_t parentcc;
	uint32_t parentf;

	TContactKey key;

	m_Sync.BeginWrite();
	if (!m_BlockManager.ReadBlock(hContact, pContact, size, sig) ||
		  !m_BlockManager.ReadPart(Contact.ParentContact, &parentcc, offsetof(TContact, ChildCount), sizeof(parentcc), sig) ||
			!m_BlockManager.ReadPart(Contact.ParentContact, &parentf, offsetof(TContact, Flags), sizeof(parentf), sig))
	{
		m_Sync.EndWrite();
		return DB_INVALIDPARAM;
	}

	if (Contact.Flags & DB_CF_HasVirtuals)
	{
		// move virtuals and make one of them real
		TDBContactHandle newreal = m_Virtuals._DeleteRealContact(hContact);
		
		m_BlockManager.WritePartCheck(newreal, &Contact.EventCount, offsetof(TContact, EventCount), sizeof(uint32_t), sig);
		m_BlockManager.WritePart(newreal, &Contact.Events, offsetof(TContact, Events), sizeof(uint32_t));
		m_BlockManager.WritePart(newreal, &Contact.Settings, offsetof(TContact, Settings), sizeof(/*CSettingsTree::TNodeRef*/ uint32_t));

	} else {
		// TODO delete settings and events
	}

	key.Level = Contact.Level;
	key.Parent = Contact.ParentContact;
	key.Contact = hContact;
	Delete(key);
	
	if (Contact.Flags & DB_CF_HasChildren) // keep the children
	{
		parentf = parentf | DB_CF_HasChildren;
		parentcc += Contact.ChildCount;

		TDBContactIterFilter filter = {0};
		filter.cbSize = sizeof(filter);
		filter.Options = DB_CIFO_OSC_AC | DB_CIFO_OC_AC;

		TDBContactIterationHandle iter = IterationInit(filter, hContact);
		if (iter != DB_INVALIDPARAM)
		{
			IterationNext(iter);
			key.Contact = IterationNext(iter);
			
			while ((key.Contact != 0) && (key.Contact != DB_INVALIDPARAM))
			{
				if (m_BlockManager.ReadPart(key.Contact, &key.Parent, offsetof(TContact, ParentContact), sizeof(key.Parent), sig) &&
					m_BlockManager.ReadPart(key.Contact, &key.Level, offsetof(TContact, Level), sizeof(key.Level), sig))
				{
					Delete(key);

					if (key.Parent == hContact)
					{
						key.Parent = Contact.ParentContact;
						m_BlockManager.WritePart(key.Contact, &key.Parent, offsetof(TContact, ParentContact), sizeof(key.Parent));
					}
					
					key.Level--;
					m_BlockManager.WritePart(key.Contact, &key.Level, offsetof(TContact, Level), sizeof(key.Level));
					Insert(key, TEmpty());

				}
				key.Contact = IterationNext(iter);
			}

			IterationClose(iter);
		}
	}

	m_BlockManager.DeleteBlock(hContact); // we need this block to start iteration, delete it here
	--parentcc;

	if (parentcc == 0)
		Contact.Flags = Contact.Flags & (~DB_CF_HasChildren);

	m_BlockManager.WritePartCheck(Contact.ParentContact, &parentcc, offsetof(TContact, ChildCount), sizeof(parentcc), sig);
	m_BlockManager.WritePart(Contact.ParentContact, &parentf, offsetof(TContact, Flags), sizeof(parentf));

	m_Sync.EndWrite();
	return 0;
}



TDBContactIterationHandle CContacts::IterationInit(const TDBContactIterFilter & Filter, TDBContactHandle hParent)
{
	uint16_t l;
	uint32_t f;
	uint32_t sig = cContactSignature;

	m_Sync.BeginWrite();
	
	if (!m_BlockManager.ReadPart(hParent, &l, offsetof(TContact, Level), sizeof(l), sig) ||
	    !m_BlockManager.ReadPart(hParent, &f, offsetof(TContact, Flags), sizeof(f), sig))
	{
		m_Sync.EndWrite();
		return DB_INVALIDPARAM;
	}

	unsigned int i = 0;

	while ((i < m_Iterations.size()) && (m_Iterations[i] != NULL))
		++i;

	if (i == m_Iterations.size())
		m_Iterations.push_back(NULL);


	PContactIteration iter = new TContactIteration;
	iter->filter = Filter;
	iter->q = new std::deque<TContactIterationItem>;
	iter->parents = new std::deque<TContactIterationItem>;
	iter->returned = new stdext::hash_set<TDBContactHandle>;
	iter->returned->insert(hParent);
	
	TContactIterationItem it;
	it.Flags = f;
	it.Handle = hParent;
	it.Level = l;
	it.Options = Filter.Options & 0x000000ff;
	it.LookupDepth = 0;

	iter->q->push_back(it);

	m_Iterations[i] = iter;

	m_Sync.EndWrite();
	return i + 1;
}
TDBContactHandle CContacts::IterationNext(TDBContactIterationHandle Iteration)
{
	uint32_t sig = cContactSignature;

	m_Sync.BeginRead();

	if (Iteration == 0)
		return 0;

	if ((Iteration > m_Iterations.size()) || (m_Iterations[Iteration - 1] == NULL))
	{
		m_Sync.EndRead();
		return DB_INVALIDPARAM;
	}

	PContactIteration iter = m_Iterations[Iteration - 1];
	TContactIterationItem item;
	TDBContactHandle result = 0;

	if (iter->q->empty())
	{
		std::deque <TContactIterationItem> * tmp = iter->q;
		iter->q = iter->parents;
		iter->parents = tmp;
	}

	if (iter->q->empty() && 
		  (iter->filter.Options & DB_CIFO_GF_USEROOT) &&
			(iter->returned->find(m_RootContact) == iter->returned->end()))
	{
		item.Handle = m_RootContact;
		item.Level = 0;
		item.Options = 0;
		item.Flags = 0;
		item.LookupDepth = 255;

		iter->filter.Options = iter->filter.Options & ~DB_CIFO_GF_USEROOT;

		iter->q->push_back(item);
	}

	if (iter->q->empty())
	{
		m_Sync.EndRead();
		return 0;
	}
	
	do {
		item = iter->q->front();
		iter->q->pop_front();

		std::deque<TContactIterationItem> tmp;
		TContactIterationItem newitem;

		// children
		if ((item.Flags & DB_CF_HasChildren) &&
		  	(item.Options & DB_CIFO_OSC_AC))
		{
			TContactKey key;
			key.Parent = item.Handle;
			key.Level = item.Level + 1;

			newitem.Level = item.Level + 1;
			newitem.LookupDepth = item.LookupDepth;
			newitem.Options = (iter->filter.Options / DB_CIFO_OC_AC * DB_CIFO_OSC_AC) & (DB_CIFO_OSC_AC | DB_CIFO_OSC_AO | DB_CIFO_OSC_AOC | DB_CIFO_OSC_AOP);

			if (iter->filter.Options & DB_CIFO_GF_DEPTHFIRST)
			{
				key.Contact = 0xffffffff;

				iterator c = UpperBound(key);
				while ((c) && (c.Key().Parent == item.Handle))
				{
					newitem.Handle = c.Key().Contact;
					
					if ((iter->returned->find(newitem.Handle) == iter->returned->end()) &&
						  m_BlockManager.ReadPart(newitem.Handle, &newitem.Flags, offsetof(TContact, Flags), sizeof(newitem.Flags), sig) &&
						  (((newitem.Flags & DB_CF_IsGroup) == 0) || ((DB_CF_IsGroup & iter->filter.fHasFlags) == 0))) // if we want only groups, we don't need to trace down contacts...
					{
						iter->q->push_front(newitem);
						iter->returned->insert(newitem.Handle);
					}

					--c;
				}
			} else {
				key.Contact = 0;

				iterator c = LowerBound(key);
				while ((c) && (c.Key().Parent == item.Handle))
				{
					newitem.Handle = c.Key().Contact;
					
					if ((iter->returned->find(newitem.Handle) == iter->returned->end()) &&
						  m_BlockManager.ReadPart(newitem.Handle, &newitem.Flags, offsetof(TContact, Flags), sizeof(newitem.Flags), sig) &&
						  (((newitem.Flags & DB_CF_IsGroup) == 0) || ((DB_CF_IsGroup & iter->filter.fHasFlags) == 0))) // if we want only groups, we don't need to trace down contacts...
					{
						iter->q->push_back(newitem);
						iter->returned->insert(newitem.Handle);
					}

					++c;
				}

			}
		}

		// parent...
		if ((item.Options & DB_CIFO_OSC_AP) && (item.Handle != m_RootContact))
		{
			newitem.Handle = getParent(item.Handle);
			if ((iter->returned->find(newitem.Handle) == iter->returned->end()) &&
				  (newitem.Handle != DB_INVALIDPARAM) &&
				  m_BlockManager.ReadPart(newitem.Handle, &newitem.Flags, offsetof(TContact, Flags), sizeof(newitem.Flags), sig))
			{
				newitem.Level = item.Level - 1;
				newitem.LookupDepth = item.LookupDepth;
				newitem.Options = (iter->filter.Options / DB_CIFO_OP_AC * DB_CIFO_OSC_AC) & (DB_CIFO_OSC_AC | DB_CIFO_OSC_AP | DB_CIFO_OSC_AO | DB_CIFO_OSC_AOC | DB_CIFO_OSC_AOP);

				if ((newitem.Flags & iter->filter.fDontHasFlags & DB_CF_IsGroup) == 0) // if we don't want groups, stop it
				{
					iter->parents->push_back(newitem);
					iter->returned->insert(newitem.Handle);
				}
			}
		}

		// virtual lookup, original contact is the next one
		if ((item.Flags & DB_CF_IsVirtual) && 
			  (item.Options & DB_CIFO_OSC_AO) && 
				(((iter->filter.Options >> 28) >= item.LookupDepth) || ((iter->filter.Options >> 28) == 0)))
		{
			newitem.Handle = VirtualGetParent(item.Handle);

			if ((iter->returned->find(newitem.Handle) == iter->returned->end()) &&
				  (newitem.Handle != DB_INVALIDPARAM) &&
				   m_BlockManager.ReadPart(newitem.Handle, &newitem.Flags, offsetof(TContact, Flags), sizeof(newitem.Flags), sig) &&
           m_BlockManager.ReadPart(newitem.Handle, &newitem.Level, offsetof(TContact, Level), sizeof(newitem.Level), sig))
			{
				newitem.Options  = 0;
				if ((item.Options & DB_CIFO_OSC_AOC) == DB_CIFO_OSC_AOC)
					newitem.Options |= DB_CIFO_OSC_AC;
				if ((item.Options & DB_CIFO_OSC_AOP) == DB_CIFO_OSC_AOP)
					newitem.Options |= DB_CIFO_OSC_AP;

				newitem.LookupDepth = item.LookupDepth + 1;

				iter->q->push_front(newitem);
				iter->returned->insert(newitem.Handle);
			}
		}

		if (((iter->filter.fHasFlags & item.Flags) == iter->filter.fHasFlags) &&
		    ((iter->filter.fDontHasFlags & item.Flags) == 0))
		{
			result = item.Handle;
		}

	} while ((result == 0) && !iter->q->empty());



	if (result == 0)
		result = IterationNext(Iteration);


	m_Sync.EndRead();

	return result;
}
unsigned int CContacts::IterationClose(TDBContactIterationHandle Iteration)
{
	m_Sync.BeginWrite();

	if ((Iteration > m_Iterations.size()) || (Iteration == 0) || (m_Iterations[Iteration - 1] == NULL))
	{
		m_Sync.EndWrite();
		return DB_INVALIDPARAM;
	}

	delete m_Iterations[Iteration - 1]->q;
	delete m_Iterations[Iteration - 1]->parents;
	delete m_Iterations[Iteration - 1]->returned;
	delete m_Iterations[Iteration - 1];
	m_Iterations[Iteration - 1] = NULL;

	m_Sync.EndWrite();
	return 0;
}



TDBContactHandle CContacts::VirtualCreate(TDBContactHandle hRealContact, TDBContactHandle hParent)
{
	uint32_t f;
	uint32_t sig = cContactSignature;

	m_Sync.BeginWrite();
	
	if (!m_BlockManager.ReadPart(hRealContact, &f, offsetof(TContact, Flags), sizeof(f), sig) || 
		 (f & DB_CF_IsGroup))
	{
		m_Sync.EndWrite();
		return DB_INVALIDPARAM;
	}

	TDBContactHandle result = CreateContact(hParent, DB_CF_IsVirtual);
	if (result == DB_INVALIDPARAM)
	{
		m_Sync.EndWrite();
		return DB_INVALIDPARAM;
	}

	if (f & DB_CF_IsVirtual)
	{		
		m_BlockManager.ReadPart(hRealContact, &hRealContact, offsetof(TContact, VParent), sizeof(hRealContact), sig);		
		m_BlockManager.ReadPart(hRealContact, &f, offsetof(TContact, Flags), sizeof(f), sig);
	}

	m_BlockManager.WritePart(result, &hRealContact, offsetof(TContact, VParent), sizeof(hRealContact));

	if ((f & DB_CF_HasVirtuals) == 0)
	{
		f |= DB_CF_HasVirtuals;
		m_BlockManager.WritePart(hRealContact, &f, offsetof(TContact, Flags), sizeof(f));
	}

	m_Virtuals._InsertVirtual(hRealContact, result);

	m_Sync.EndWrite();
	return result;
}

TDBContactHandle CContacts::VirtualGetParent(TDBContactHandle hVirtual)
{
	return m_Virtuals.getParent(hVirtual);
}
TDBContactHandle CContacts::VirtualGetFirst(TDBContactHandle hRealContact)
{
	return m_Virtuals.getFirst(hRealContact);
}
TDBContactHandle CContacts::VirtualGetNext(TDBContactHandle hVirtual)
{	
	return m_Virtuals.getNext(hVirtual);
}