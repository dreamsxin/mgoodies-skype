#pragma once
#include "BTree.h"
#include "BlockManager.h"

template <typename TKey, int SizeParam = 4>
class CFileBTree :	public CBTree<TKey, SizeParam>
{
private:
	
protected:
	CBlockManager & m_BlockManager;
	uint32_t cSignature;

	virtual TNodeRef CreateNewNode();
	virtual void DeleteNode(TNodeRef Node);
	virtual void Read(TNodeRef Node, uint32_t Offset, uint32_t Size, TNode & Dest);
	virtual void Write(TNodeRef Node, uint32_t Offset, uint32_t Size, TNode & Source);
	
public:
	CFileBTree(CBlockManager & BlockManager, TNodeRef RootNode, uint16_t Signature);
	virtual ~CFileBTree();
};




template <typename TKey, int SizeParam>
CFileBTree<TKey, SizeParam>::CFileBTree(CBlockManager & BlockManager, TNodeRef RootNode, uint16_t Signature)
:	CBTree(RootNode),
	m_BlockManager(BlockManager)
{
	cSignature = Signature << 16;
	m_DestroyTree = false;
}

template <typename TKey, int SizeParam>
CFileBTree<TKey, SizeParam>::~CFileBTree()
{

}

template <typename TKey, int SizeParam>
typename CFileBTree<TKey, SizeParam>::TNodeRef CFileBTree<TKey, SizeParam>::CreateNewNode()
{
	return m_BlockManager.CreateBlock(sizeof(TNode) - 2, cSignature);
}

template <typename TKey, int SizeParam>
void CFileBTree<TKey, SizeParam>::DeleteNode(TNodeRef Node)
{
	m_BlockManager.DeleteBlock(Node);
}

template <typename TKey, int SizeParam>
void CFileBTree<TKey, SizeParam>::Read(TNodeRef Node, uint32_t Offset, uint32_t Size, TNode & Dest)
{
	uint32_t sig = 0;
	if (Offset == 0)
	{
		m_BlockManager.ReadPart(Node, (uint16_t*)&Dest + 1, 0, Size - 2, sig);
	} else {
		m_BlockManager.ReadPart(Node, (uint8_t*)&Dest + Offset, Offset - 2, Size, sig);		
	}
	Dest.Info = sig & 0xffff;
	sig = sig & 0xffff0000;

	if (sig != cSignature)
		throwException("Signature check failed");

}

template <typename TKey, int SizeParam>
void CFileBTree<TKey, SizeParam>::Write(TNodeRef Node, uint32_t Offset, uint32_t Size, TNode & Source)
{
	if (Offset == 0)
	{
		m_BlockManager.WritePart(Node, (uint16_t*)&Source + 1, 0, Size - 2, cSignature | Source.Info);
	} else {
		m_BlockManager.WritePart(Node, (uint8_t*)&Source + Offset, Offset - 2, Size);		
	}
}
