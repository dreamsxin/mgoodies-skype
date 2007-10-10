#pragma once

#include <windows.h>
#include <map>
#include "FileAccess.h"

class CMappedMemory : public CFileAccess
{
private:
	typedef std::multimap<unsigned int, unsigned int> TFreeSpaceMap;	
	TFreeSpaceMap m_FreeSpace;
	TFreeSpaceMap::iterator m_FreeFileEnd;

	char* m_Base;
	unsigned int m_Size;
	unsigned int m_AllocSize;
	unsigned int m_AllocGranularity;

	HANDLE m_DirectFile;
	HANDLE m_FileMapping;
protected:
	void Map();

public:
	CMappedMemory(const char* FileName);
	virtual ~CMappedMemory();

	unsigned int Read(void* Buf, unsigned int Source, unsigned int Size);
  unsigned int Write(void* Buf, unsigned int Dest, unsigned int Size);
	unsigned int Move(unsigned int Source, unsigned int Dest, unsigned int Size);

	unsigned int Alloc(unsigned int Size);
	void Free(unsigned int Dest, unsigned int Count);

};
