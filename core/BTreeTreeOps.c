/*
 * Copyright (c) 2000-2015 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
	File:		BTreeTreeOps.c

	Contains:	Multi-node tree operations for the BTree Module.

	Version:	xxx put the technology version here xxx

	Written by:	Gordon Sheridan and Bill Bruffey

	Copyright:	(c) 1992-1999 by Apple Inc., all rights reserved.

	File Ownership:

		DRI:				Don Brady

		Other Contact:		Mark Day

		Technology:			File Systems

	Writers:

		(msd)	Mark Day
		(DSH)	Deric Horn
		(djb)	Don Brady

	Change History (most recent first):

	   <MOSXS>	  6/1/99	djb		Sync up with Mac OS 8.6.
	   <CS5>	 12/8/97	djb		Radar #2200632, CollapseTree wasn't marking root node dirty.
	   <CS4>	11/24/97	djb		Radar #2005325, InsertLevel incorrectly handled root splits!
	   <CS3>	10/17/97	msd		Conditionalize DebugStrs.
	   <CS2>	 5/16/97	msd		InsertNode() needs a return statement in ErrorExit.
	   <CS1>	 4/23/97	djb		first checked in

	  <HFS8>	 3/17/97	DSH		Conditionalize out Panic assertion for SC.
	  <HFS7>	  3/3/97	djb		Removed DebugStr in InsertLevel.
	  <HFS6>	 2/19/97	djb		Major re-write of insert code; added InsertLevel and InsertNode.
	  <HFS5>	 1/27/97	djb		InsertTree and DeleteTree are now recursive and support variable
									sized index keys.
	  <HFS4>	 1/16/97	djb		Removed DebugStr in SearchTree. Added initial support for
									variable sized index keys.
	  <HFS3>	  1/3/97	djb		Changed len8 to length8.
	  <HFS2>	  1/3/97	djb		Added support for large keys.
	  <HFS1>	12/19/96	djb		first checked in

	History applicable to original Scarecrow Design:

		 <3>	10/25/96	ser		Changing for new VFPI
		 <2>	 1/22/96	dkh		Add #include Memory.h
		 <1>	10/18/95	rst		Moved from Scarecrow project.

		<12>	 7/18/95	mbb		Change MoveData & ClearBytes to BlockMoveData & BlockZero.
		<11>	 9/30/94	prp		Get in sync with D2 interface changes.
		<10>	 7/25/94	wjk		Eliminate usage of BytePtr in favor of UInt8 *.
		 <9>	 7/22/94	wjk		Convert to the new set of header files.
		 <8>	 12/2/93	wjk		Move from Makefiles to BuildFiles. Fit into the ModernOS and
									NRCmds environments.
		 <7>	11/30/93	wjk		Change some Ptr's to BytePtr's in function definitions so they
									agree with their prototypes.
		 <6>	 5/21/93	gs		Debug DeleteTree. Modify InsertTree for BTReplaceRecord.
		 <5>	 5/10/93	gs		Modify RotateLeft, and add DeleteTree, CollapseTree routines.
		 <4>	 3/23/93	gs		revise RotateLeft to use InsertKeyRecord instead of
									InsertRecord.
		 <3>	 3/23/93	gs		Implement SplitLeft, InsertTree routine.
		 <2>	  2/8/93	gs		Implement SearchTree, and RotateLeft.
		 <1>	11/15/92	gs		first checked in

*/

#include "BTreesPrivate.h"
#include "hfs_btreeio.h"

//
/////////////////////// Routines Internal To BTree Module ///////////////////////
//
//	SearchTree
//	InsertTree
//
////////////////////// Routines Internal To BTreeTreeOps.c //////////////////////

static OSStatus   AddNewRootNode	(BTreeControlBlockPtr		 btreePtr,
									 NodeDescPtr				 leftNode,
									 NodeDescPtr				 rightNode );

static OSStatus   CollapseTree		(BTreeControlBlockPtr		 btreePtr,
									 BlockDescriptor			*blockPtr );

static OSStatus   RotateLeft		(BTreeControlBlockPtr		 btreePtr,
									 NodeDescPtr				 leftNode,
									 NodeDescPtr				 rightNode,
									 u_int16_t					 rightInsertIndex,
									 KeyPtr						 keyPtr,
									 u_int8_t *					 recPtr,
									 u_int16_t					 recSize,
									 u_int16_t					*insertIndex,
									 u_int32_t					*insertNodeNum,
									 Boolean					*recordFit,
									 u_int16_t					*recsRotated );

static Boolean	   RotateRecordLeft	(BTreeControlBlockPtr		 btreePtr,
									 NodeDescPtr				 leftNode,
									 NodeDescPtr				 rightNode );

static OSStatus	   SplitLeft		(BTreeControlBlockPtr		 btreePtr,
									 BlockDescriptor			*leftNode,
									 BlockDescriptor			*rightNode,
									 u_int32_t					 rightNodeNum,
									 u_int16_t					 index,
									 KeyPtr						 keyPtr,
									 u_int8_t *					 recPtr,
									 u_int16_t					 recSize,
									 u_int16_t					*insertIndex,
									 u_int32_t					*insertNodeNum,
									 u_int16_t					*recsRotated );
								 


static	OSStatus	InsertLevel		(BTreeControlBlockPtr		 btreePtr,
									 TreePathTable				 treePathTable,
									 InsertKey					*primaryKey,
									 InsertKey					*secondaryKey,
									 BlockDescriptor			*targetNode,
									 u_int16_t					 index,
									 u_int16_t					 level,
									 u_int32_t					*insertNode );
						 
static OSErr		InsertNode 		(BTreeControlBlockPtr		 btreePtr,
									 InsertKey					*key,
									 BlockDescriptor			*rightNode,
									 u_int32_t					 node,
									 u_int16_t	 				 index,
									 u_int32_t					*newNode,	
									 u_int16_t					*newIndex,
									 BlockDescriptor			*leftNode,
									 Boolean					*updateParent,
									 Boolean					*insertParent,
									 Boolean					*rootSplit );
									 
static u_int16_t		GetKeyLength	(const BTreeControlBlock *btreePtr,
									 const BTreeKey *key,
									 Boolean forLeafNode );



//////////////////////// BTree Multi-node Tree Operations ///////////////////////


/*-------------------------------------------------------------------------------

Routine:	SearchTree	-	Search BTree for key and set up Tree Path Table.

Function:	Searches BTree for specified key, setting up the Tree Path Table to
			reflect the search path.


Input:		btreePtr		- pointer to control block of BTree to search
			keyPtr			- pointer to the key to search for
			treePathTable	- pointer to the tree path table to construct
			
Output:		nodeNum			- number of the node containing the key position
			iterator		- BTreeIterator specifying record or insert position
			
Result:		noErr			- key found, index is record index
			fsBTRecordNotFoundErr	- key not found, index is insert index
			fsBTEmptyErr		- key not found, return params are nil
			otherwise			- catastrophic failure (GetNode/ReleaseNode failed)
-------------------------------------------------------------------------------*/

OSStatus	SearchTree	(BTreeControlBlockPtr	 btreePtr,
						 BTreeKeyPtr			 searchKey,
						 TreePathTable			 treePathTable,
						 u_int32_t				*nodeNum,
						 BlockDescriptor		*nodePtr,
						 u_int16_t				*returnIndex )
{
	OSStatus	err;
	int16_t		level;					//	Expected depth of current node
	u_int32_t	curNodeNum;				//	Current node we're searching
	NodeRec		nodeRec;
	u_int16_t	index;
	Boolean		keyFound;
	int8_t		nodeKind;				//	Kind of current node (index/leaf)
	KeyPtr		keyPtr;
	u_int8_t *	dataPtr;
	u_int16_t	dataSize;
	
	
	curNodeNum		= btreePtr->rootNode;
	level			= btreePtr->treeDepth;
	
	if (level == 0)						// is the tree empty?
	{
		err = fsBTEmptyErr;
		goto ErrorExit;
	}
	
	//�� for debugging...
	treePathTable [0].node		= 0;
	treePathTable [0].index		= 0;

	while (true)
	{
        //
        //	[2550929] Node number 0 is the header node.  It is never a valid
        //	index or leaf node.  If we're ever asked to search through node 0,
        //	something has gone wrong (typically a bad child node number, or
        //	we found a node full of zeroes that we thought was an index node).
        //
        if (curNodeNum == 0)
        {
//          Panic("SearchTree: curNodeNum is zero!");
            err = btBadNode;
            goto ErrorExit;
        }
        
        err = GetNode (btreePtr, curNodeNum, 0, &nodeRec);
        if (err != noErr)
        {
                goto ErrorExit;
        }
		
        //
        //	[2550929] Sanity check the node height and node type.  We expect
        //	particular values at each iteration in the search.  This checking
        //	quickly finds bad pointers, loops, and other damage to the
        //	hierarchy of the B-tree.
        //
        if (((BTNodeDescriptor*)nodeRec.buffer)->height != level)
        {
//		Panic("Incorrect node height");
                err = btBadNode;
                goto ReleaseAndExit;
        }
        nodeKind = ((BTNodeDescriptor*)nodeRec.buffer)->kind;
        if (level == 1)
        {
            //	Nodes at level 1 must be leaves, by definition
            if (nodeKind != kBTLeafNode)
            {
 //		Panic("Incorrect node type: expected leaf");
                err = btBadNode;
                goto ReleaseAndExit;           
            }
        }
        else
        {
            //	A node at any other depth must be an index node
            if (nodeKind != kBTIndexNode)
            {
//		Panic("Incorrect node type: expected index");
                err = btBadNode;
                goto ReleaseAndExit;
            }
        }
        
        keyFound = SearchNode (btreePtr, nodeRec.buffer, searchKey, &index);

        treePathTable [level].node		= curNodeNum;

        if (nodeKind == kBTLeafNode)
        {
                treePathTable [level].index = index;
                break;			// were done...
        }
        
        if ( (keyFound != true) && (index != 0))
                --index;

        treePathTable [level].index = index;
        
        err = GetRecordByIndex (btreePtr, nodeRec.buffer, index, &keyPtr, &dataPtr, &dataSize);
        if (err != noErr)
        {
            //	[2550929] If we got an error, it is probably because the index was bad
            //	(typically a corrupt node that confused SearchNode).  Invalidate the node
            //	so we won't accidentally use the corrupted contents.  NOTE: the Mac OS 9
            //	sources call this InvalidateNode.
            
                (void) TrashNode(btreePtr, &nodeRec);
                goto ErrorExit;
        }
        
        //	Get the child pointer out of this index node.  We're now done with the current
        //	node and can continue the search with the child node.
        curNodeNum = *(u_int32_t *)dataPtr;
        err = ReleaseNode (btreePtr, &nodeRec);
        if (err != noErr)
        {
                goto ErrorExit;
        }

        //	The child node should be at a level one less than the parent.
        --level;
	}
	
	*nodeNum			= curNodeNum;
	*nodePtr			= nodeRec;
	*returnIndex		= index;

	if (keyFound)
		return	noErr;			// searchKey found, index identifies record in node
	else
		return	fsBTRecordNotFoundErr;	// searchKey not found, index identifies insert point

ReleaseAndExit:
    (void) ReleaseNode(btreePtr, &nodeRec);
    //	fall into ErrorExit

ErrorExit:
	
	*nodeNum					= 0;
	nodePtr->buffer				= nil;
	nodePtr->blockHeader		= nil;
	*returnIndex				= 0;

	return	err;
}




////////////////////////////////// InsertTree ///////////////////////////////////

OSStatus	InsertTree ( BTreeControlBlockPtr		 btreePtr,
						 TreePathTable				 treePathTable,
						 KeyPtr						 keyPtr,
						 u_int8_t *					 recPtr,
						 u_int16_t					 recSize,
						 BlockDescriptor			*targetNode,
						 u_int16_t					 index,
						 u_int16_t					 level,
						 Boolean					 replacingKey,
						 u_int32_t					*insertNode )
{
	InsertKey			primaryKey;
	OSStatus			err;

	primaryKey.keyPtr		= keyPtr;
	primaryKey.keyLength	= GetKeyLength(btreePtr, primaryKey.keyPtr, (level == 1));
	primaryKey.recPtr		= recPtr;
	primaryKey.recSize		= recSize;
	primaryKey.replacingKey	= replacingKey;
	primaryKey.skipRotate	= false;

	err	= InsertLevel (btreePtr, treePathTable, &primaryKey, nil,
					   targetNode, index, level, insertNode );
						
	return err;

} // End of InsertTree


////////////////////////////////// InsertLevel //////////////////////////////////

OSStatus	InsertLevel (BTreeControlBlockPtr		 btreePtr,
						 TreePathTable				 treePathTable,
						 InsertKey					*primaryKey,
						 InsertKey					*secondaryKey,
						 BlockDescriptor			*targetNode,
						 u_int16_t					 index,
						 u_int16_t					 level,
						 u_int32_t					*insertNode )
{
	OSStatus			 err;
	BlockDescriptor		 leftNode;
	u_int32_t			 targetNodeNum;
	u_int32_t			 newNodeNum;
	u_int16_t			 newIndex;
	Boolean				 insertParent;
	Boolean				 updateParent;
	Boolean				 newRoot;
	InsertKey			insertKey;

#if defined(applec) && !defined(__SC__)
	PanicIf ((level == 1) && (((NodeDescPtr)targetNode->buffer)->kind != kBTLeafNode), " InsertLevel: non-leaf at level 1! ");
#endif
	leftNode.buffer = nil;
	leftNode.blockHeader = nil;
	targetNodeNum = treePathTable [level].node;

	insertParent = false;
	updateParent = false;

	// XXXdbg
	ModifyBlockStart(btreePtr->fileRefNum, targetNode);

	////// process first insert //////

	err = InsertNode (btreePtr, primaryKey, targetNode, targetNodeNum, index,
					  &newNodeNum, &newIndex, &leftNode, &updateParent, &insertParent, &newRoot );
	M_ExitOnError (err);

	if ( newRoot )
	{
		// Extend the treePathTable by adding an entry for the new
		// root node that references the current targetNode.
		// 
		// If inserting the secondaryKey changes the first key of
		// the target node, then we'll have to update the second
		// key in the new root node.

		treePathTable [level + 1].node  = btreePtr->rootNode;
		treePathTable [level + 1].index = 1;	// 1 since we always split/rotate left
	}
	
	if ( level == 1 )
		*insertNode = newNodeNum;		
	
	////// process second insert (if any) //////

	if  ( secondaryKey != nil )
	{
		Boolean				temp;

		err = InsertNode (btreePtr, secondaryKey, targetNode, newNodeNum, newIndex,
						  &newNodeNum, &newIndex, &leftNode, &updateParent, &insertParent, &temp);
		M_ExitOnError (err);
	}

	//////////////////////// Update Parent(s) ///////////////////////////////

	if ( insertParent || updateParent )
	{
		BlockDescriptor		parentNode;
		u_int32_t			parentNodeNum;
		KeyPtr				keyPtr;
		u_int8_t *			recPtr;
		u_int16_t			recSize;
		
		parentNode.buffer = nil;
		parentNode.blockHeader = nil;

		secondaryKey = nil;
		
		PanicIf ( (level == btreePtr->treeDepth), " InsertLevel: unfinished insert!?");

		++level;

		// Get Parent Node data...
		index = treePathTable [level].index;
		parentNodeNum = treePathTable [level].node;

		PanicIf ( parentNodeNum == 0, " InsertLevel: parent node is zero!?");

		err = GetNode (btreePtr, parentNodeNum, 0, &parentNode);	// released as target node in next level up
		M_ExitOnError (err);
		////////////////////////// Update Parent Index //////////////////////////////
	
		if ( updateParent )
		{
			// XXXdbg
			ModifyBlockStart(btreePtr->fileRefNum, &parentNode);

			//���debug: check if ptr == targetNodeNum
			GetRecordByIndex (btreePtr, parentNode.buffer, index, &keyPtr, &recPtr, &recSize);
			PanicIf( (*(u_int32_t *) recPtr) != targetNodeNum, " InsertLevel: parent ptr doesn't match target node!");
			
			// need to delete and re-insert this parent key/ptr
			// we delete it here and it gets re-inserted in the
			// InsertLevel call below.
			DeleteRecord (btreePtr, parentNode.buffer, index);
	
			primaryKey->keyPtr		 = (KeyPtr) GetRecordAddress( btreePtr, targetNode->buffer, 0 );
			primaryKey->keyLength	 = GetKeyLength(btreePtr, primaryKey->keyPtr, false);
			primaryKey->recPtr		 = (u_int8_t *) &targetNodeNum;
			primaryKey->recSize		 = sizeof(targetNodeNum);
			primaryKey->replacingKey = kReplaceRecord;
			primaryKey->skipRotate   = insertParent;		// don't rotate left if we have two inserts occuring
		}
	
		////////////////////////// Add New Parent Index /////////////////////////////
	
		if ( insertParent )
		{
			InsertKey	*insertKeyPtr;
			
			if ( updateParent )
			{
				insertKeyPtr = &insertKey;
				secondaryKey = &insertKey;
			}
			else
			{
				insertKeyPtr = primaryKey;
			}
			
			insertKeyPtr->keyPtr		= (KeyPtr) GetRecordAddress (btreePtr, leftNode.buffer, 0);
			insertKeyPtr->keyLength		= GetKeyLength(btreePtr, insertKeyPtr->keyPtr, false);
			insertKeyPtr->recPtr		= (u_int8_t *) &((NodeDescPtr)targetNode->buffer)->bLink;
			insertKeyPtr->recSize		= sizeof(u_int32_t);
			insertKeyPtr->replacingKey	= kInsertRecord;
			insertKeyPtr->skipRotate	= false;		// a rotate is OK during second insert
		}	
		
		err = InsertLevel (btreePtr, treePathTable, primaryKey, secondaryKey,
						   &parentNode, index, level, insertNode );
		M_ExitOnError (err);
	}

	err = UpdateNode (btreePtr, targetNode, 0, kLockTransaction);	// all done with target
	M_ExitOnError (err);

	err = UpdateNode (btreePtr, &leftNode, 0, kLockTransaction);		// all done with left sibling
	M_ExitOnError (err);
	
	return	noErr;

ErrorExit:

	(void) ReleaseNode (btreePtr, targetNode);
	(void) ReleaseNode (btreePtr, &leftNode);

	Panic (" InsertLevel: an error occurred!");

	return	err;

} // End of InsertLevel



////////////////////////////////// InsertNode ///////////////////////////////////

static OSErr	InsertNode	(BTreeControlBlockPtr	 btreePtr,
							 InsertKey				*key,

							 BlockDescriptor		*rightNode,
							 u_int32_t				 node,
							 u_int16_t	 			 index,

							 u_int32_t				*newNode,	
							 u_int16_t				*newIndex,

							 BlockDescriptor		*leftNode,
							 Boolean				*updateParent,
							 Boolean				*insertParent,
							 Boolean				*rootSplit )
{
	BlockDescriptor		*targetNode = NULL;
	u_int32_t			 leftNodeNum;
	u_int16_t			 recsRotated;
	OSErr				 err;
	Boolean				 recordFit;

	*rootSplit = false;
	
	PanicIf ( rightNode->buffer == leftNode->buffer, " InsertNode: rightNode == leftNode, huh?");
	
	leftNodeNum = ((NodeDescPtr) rightNode->buffer)->bLink;


	/////////////////////// Try Simple Insert ///////////////////////////////

	/* sanity check our left and right nodes here. */
	if (node == leftNodeNum) {
		if (leftNode->buffer == NULL) {
			err = fsBTInvalidNodeErr;
			M_ExitOnError(err);	
		}
		else{
			targetNode = leftNode;
		}
	}
	else {
		// we can assume right node is initialized.
		targetNode = rightNode;	
	}
	
	
	recordFit = InsertKeyRecord (btreePtr, targetNode->buffer, index, key->keyPtr, key->keyLength, key->recPtr, key->recSize);

	if ( recordFit )
	{
		*newNode  = node;
		*newIndex = index;
	
		if ( (index == 0) && (((NodeDescPtr) targetNode->buffer)->height != btreePtr->treeDepth) )
			*updateParent = true;	// the first record changed so we need to update the parent
	}


	//////////////////////// Try Rotate Left ////////////////////////////////
	
	if ( !recordFit && leftNodeNum > 0 )
	{
		PanicIf ( leftNode->buffer != nil, " InsertNode: leftNode already acquired!");

		if ( leftNode->buffer == nil )
		{
			err = GetNode (btreePtr, leftNodeNum, 0, leftNode);	// will be released by caller or a split below
			M_ExitOnError (err);
			// XXXdbg
			ModifyBlockStart(btreePtr->fileRefNum, leftNode);
		}

		PanicIf ( ((NodeDescPtr) leftNode->buffer)->fLink != node, " InsertNode, RotateLeft: invalid sibling link!" );

		if ( !key->skipRotate )		// are rotates allowed?
		{
			err = RotateLeft (btreePtr, leftNode->buffer, rightNode->buffer, index, key->keyPtr, key->recPtr,
							  key->recSize, newIndex, newNode, &recordFit, &recsRotated );	
			M_ExitOnError (err);

			if ( recordFit )
			{
				if ( key->replacingKey || (recsRotated > 1) || (index > 0) )
					*updateParent = true;			
			}
		}
	}	


	//////////////////////// Try Split Left /////////////////////////////////

	if ( !recordFit )
	{
		// might not have left node...
		err = SplitLeft (btreePtr, leftNode, rightNode, node, index, key->keyPtr,
						 key->recPtr, key->recSize, newIndex, newNode, &recsRotated);
		M_ExitOnError (err);

		// if we split root node - add new root
		
		if ( ((NodeDescPtr) rightNode->buffer)->height == btreePtr->treeDepth )
		{
			err = AddNewRootNode (btreePtr, leftNode->buffer, rightNode->buffer);	// Note: does not update TPT
			M_ExitOnError (err);
			*rootSplit = true;
		}
		else
		{
			*insertParent = true;

			if ( key->replacingKey || (recsRotated > 1) || (index > 0) )
				*updateParent = true;
		}
	}
	
	return noErr;

ErrorExit:
	(void) ReleaseNode (btreePtr, leftNode);
	return err;
	
} // End of InsertNode


/*-------------------------------------------------------------------------------
Routine:	DeleteTree	-	One_line_description.

Function:	Brief_description_of_the_function_and_any_side_effects

ToDo:		

Input:		btreePtr		- description
			treePathTable	- description
			targetNode		- description
			index			- description
						
Result:		noErr		- success
			!= noErr	- failure
-------------------------------------------------------------------------------*/

OSStatus	DeleteTree			(BTreeControlBlockPtr		 btreePtr,
								 TreePathTable				 treePathTable,
								 BlockDescriptor			*targetNode,
								 u_int16_t					 index,
								 u_int16_t					 level )
{
	OSStatus			err;
	BlockDescriptor		parentNode;
	BTNodeDescriptor	*targetNodePtr;
	u_int32_t			targetNodeNum;
	Boolean				deleteRequired;
	Boolean				updateRequired;

	// XXXdbg - initialize these to null in case we get an
	//          error and try to exit before it's initialized
	parentNode.buffer      = nil;	
	parentNode.blockHeader = nil;
	
	deleteRequired = false;
	updateRequired = false;

	targetNodeNum = treePathTable[level].node;
	targetNodePtr = targetNode->buffer;
	PanicIf (targetNodePtr == nil, "DeleteTree: targetNode has nil buffer!");

	// XXXdbg
	ModifyBlockStart(btreePtr->fileRefNum, targetNode);

	DeleteRecord (btreePtr, targetNodePtr, index);
		
	//�� coalesce remaining records?

	if ( targetNodePtr->numRecords == 0 )	// did we delete the last record?
	{
		BlockDescriptor		siblingNode;
		u_int32_t			siblingNodeNum;

		deleteRequired = true;
		
		siblingNode.buffer = nil;
		siblingNode.blockHeader = nil;

		////////////////// Get Siblings & Update Links //////////////////////////
		
		siblingNodeNum = targetNodePtr->bLink;				// Left Sibling Node
		if ( siblingNodeNum != 0 )
		{
			err = GetNode (btreePtr, siblingNodeNum, 0, &siblingNode);
			M_ExitOnError (err);

			// XXXdbg
			ModifyBlockStart(btreePtr->fileRefNum, &siblingNode);

			((NodeDescPtr)siblingNode.buffer)->fLink = targetNodePtr->fLink;
			err = UpdateNode (btreePtr, &siblingNode, 0, kLockTransaction);
			M_ExitOnError (err);
		}
		else if ( targetNodePtr->kind == kBTLeafNode )		// update firstLeafNode
		{
			btreePtr->firstLeafNode = targetNodePtr->fLink;
		}

		siblingNodeNum = targetNodePtr->fLink;				// Right Sibling Node
		if ( siblingNodeNum != 0 )
		{
			err = GetNode (btreePtr, siblingNodeNum, 0, &siblingNode);
			M_ExitOnError (err);

			// XXXdbg
			ModifyBlockStart(btreePtr->fileRefNum, &siblingNode);

			((NodeDescPtr)siblingNode.buffer)->bLink = targetNodePtr->bLink;
			err = UpdateNode (btreePtr, &siblingNode, 0, kLockTransaction);
			M_ExitOnError (err);
		}
		else if ( targetNodePtr->kind == kBTLeafNode )		// update lastLeafNode
		{
			btreePtr->lastLeafNode = targetNodePtr->bLink;
		}
		
		//////////////////////// Free Empty Node ////////////////////////////////

		ClearNode (btreePtr, targetNodePtr);
		
		err = UpdateNode (btreePtr, targetNode, 0, kLockTransaction);
		M_ExitOnError (err);

		err = FreeNode (btreePtr, targetNodeNum);
		M_ExitOnError (err);
	}
	else if ( index == 0 )			// did we delete the first record?
	{
		updateRequired = true;		// yes, so we need to update parent
	}


	if ( level == btreePtr->treeDepth )		// then targetNode->buffer is the root node
	{
		deleteRequired = false;
		updateRequired = false;
		
		if ( targetNode->buffer == nil )	// then root was freed and the btree is empty
		{
			btreePtr->rootNode  = 0;
			btreePtr->treeDepth = 0;
		}
		else if ( ((NodeDescPtr)targetNode->buffer)->numRecords == 1 )
		{
			err = CollapseTree (btreePtr, targetNode);
			M_ExitOnError (err);
		}
	}


	if ( updateRequired || deleteRequired )
	{
		++level;	// next level

		//// Get Parent Node and index
		index = treePathTable [level].index;
		err = GetNode (btreePtr, treePathTable[level].node, 0, &parentNode);
		M_ExitOnError (err);

		if ( updateRequired )
		{
			 KeyPtr		keyPtr;
			 u_int8_t *	recPtr;
			 u_int16_t	recSize;
			 u_int32_t	insertNode;
			 
			 // XXXdbg
			 ModifyBlockStart(btreePtr->fileRefNum, &parentNode);

			//���debug: check if ptr == targetNodeNum
			GetRecordByIndex (btreePtr, parentNode.buffer, index, &keyPtr, &recPtr, &recSize);
			PanicIf( (*(u_int32_t *) recPtr) != targetNodeNum, " DeleteTree: parent ptr doesn't match targetNodeNum!!");
			
			// need to delete and re-insert this parent key/ptr
			DeleteRecord (btreePtr, parentNode.buffer, index);
	
			keyPtr = (KeyPtr) GetRecordAddress( btreePtr, targetNode->buffer, 0 );
			recPtr = (u_int8_t *) &targetNodeNum;
			recSize = sizeof(targetNodeNum);
			
			err = InsertTree (btreePtr, treePathTable, keyPtr, recPtr, recSize,
							  &parentNode, index, level, kReplaceRecord, &insertNode);
			M_ExitOnError (err);
		}
		else // deleteRequired
		{
			err = DeleteTree (btreePtr, treePathTable, &parentNode, index, level);
			M_ExitOnError (err);
		}
	}	


	err = UpdateNode (btreePtr, targetNode, 0, kLockTransaction);
	M_ExitOnError (err);

	return	noErr;

ErrorExit:

	(void) ReleaseNode (btreePtr, targetNode);
	(void) ReleaseNode (btreePtr, &parentNode);

	return	err;

} // end DeleteTree



///////////////////////////////// CollapseTree //////////////////////////////////

static OSStatus	CollapseTree	(BTreeControlBlockPtr		btreePtr,
							 	 BlockDescriptor			*blockPtr )
{
	OSStatus		err;
	u_int32_t		originalRoot;
	u_int32_t		nodeNum;
	
	originalRoot	= btreePtr->rootNode;
	
	// XXXdbg
	ModifyBlockStart(btreePtr->fileRefNum, blockPtr);

	while (true)
	{
		if ( ((NodeDescPtr)blockPtr->buffer)->numRecords > 1)
			break;							// this will make a fine root node
		
		if ( ((NodeDescPtr)blockPtr->buffer)->kind == kBTLeafNode)
			break;							// we've hit bottom
		
		nodeNum				= btreePtr->rootNode;
		btreePtr->rootNode	= GetChildNodeNum (btreePtr, blockPtr->buffer, 0);
		--btreePtr->treeDepth;

		//// Clear and Free Current Old Root Node ////
		ClearNode (btreePtr, blockPtr->buffer);
		err = UpdateNode (btreePtr, blockPtr, 0, kLockTransaction);
		M_ExitOnError (err);
		err = FreeNode (btreePtr, nodeNum);
		M_ExitOnError (err);
		
		//// Get New Root Node
		err = GetNode (btreePtr, btreePtr->rootNode, 0, blockPtr);
		M_ExitOnError (err);

		// XXXdbg
		ModifyBlockStart(btreePtr->fileRefNum, blockPtr);
	}
	
	if (btreePtr->rootNode != originalRoot)
		M_BTreeHeaderDirty (btreePtr);
		
	err = UpdateNode (btreePtr, blockPtr, 0, kLockTransaction);	// always update!
	M_ExitOnError (err);
	
	return	noErr;
	

/////////////////////////////////// ErrorExit ///////////////////////////////////

ErrorExit:
	(void)	ReleaseNode (btreePtr, blockPtr);
	return	err;
}



////////////////////////////////// RotateLeft ///////////////////////////////////

/*-------------------------------------------------------------------------------

Routine:	RotateLeft	-	One_line_description.

Function:	Brief_description_of_the_function_and_any_side_effects

Algorithm:	if rightIndex > insertIndex, subtract 1 for actual rightIndex

Input:		btreePtr			- description
			leftNode			- description
			rightNode			- description
			rightInsertIndex	- description
			keyPtr				- description
			recPtr				- description
			recSize				- description
			
Output:		insertIndex
			insertNodeNum		- description
			recordFit			- description
			recsRotated
			
Result:		noErr		- success
			!= noErr	- failure
-------------------------------------------------------------------------------*/

static OSStatus	RotateLeft		(BTreeControlBlockPtr		 btreePtr,
								 NodeDescPtr				 leftNode,
								 NodeDescPtr				 rightNode,
								 u_int16_t					 rightInsertIndex,
								 KeyPtr						 keyPtr,
								 u_int8_t *					 recPtr,
								 u_int16_t					 recSize,
								 u_int16_t					*insertIndex,
								 u_int32_t					*insertNodeNum,
								 Boolean					*recordFit,
								 u_int16_t					*recsRotated )
{
	OSStatus			err;
	int32_t				insertSize;
	int32_t				nodeSize;
	int32_t				leftSize, rightSize;
	int32_t				moveSize = 0;
	u_int16_t			keyLength;
	u_int16_t			lengthFieldSize;
	u_int16_t			index, moveIndex;
	Boolean				didItFit;

	///////////////////// Determine If Record Will Fit //////////////////////////
	
	keyLength = GetKeyLength(btreePtr, keyPtr, (rightNode->kind == kBTLeafNode));

	// the key's length field is 8-bits in HFS and 16-bits in HFS+
	if ( btreePtr->attributes & kBTBigKeysMask )
		lengthFieldSize = sizeof(u_int16_t);
	else
		lengthFieldSize = sizeof(u_int8_t);

	insertSize = keyLength + lengthFieldSize + recSize + sizeof(u_int16_t);

	if ( M_IsOdd (insertSize) )
		++insertSize;	// add pad byte;

	nodeSize		= btreePtr->nodeSize;

	// add size of insert record to right node
	rightSize		= nodeSize - GetNodeFreeSize (btreePtr, rightNode) + insertSize;
	leftSize		= nodeSize - GetNodeFreeSize (btreePtr, leftNode);

	moveIndex	= 0;

	while ( leftSize < rightSize )
	{
		if ( moveIndex < rightInsertIndex )
		{
			moveSize = GetRecordSize (btreePtr, rightNode, moveIndex) + 2;
		}
		else if ( moveIndex == rightInsertIndex )
		{
			moveSize = insertSize;
		}
		else // ( moveIndex > rightInsertIndex )
		{
			moveSize = GetRecordSize (btreePtr, rightNode, moveIndex - 1) + 2;
		}
		
		leftSize	+= moveSize;
		rightSize	-= moveSize;
		++moveIndex;
	}	
	
	if ( leftSize > nodeSize )	// undo last move
	{
		leftSize	-= moveSize;
		rightSize	+= moveSize;
		--moveIndex;
	}
	
	if ( rightSize > nodeSize )	// record won't fit - failure, but not error
	{
		*insertIndex	= 0;
		*insertNodeNum	= 0;
		*recordFit		= false;
		*recsRotated	= 0;
		
		return	noErr;
	}
	
	// we've found balance point, moveIndex == number of records moved into leftNode
	

	//////////////////////////// Rotate Records /////////////////////////////////

	*recsRotated	= moveIndex;
	*recordFit		= true;
	index			= 0;

	while ( index < moveIndex )
	{
		if ( index == rightInsertIndex )	// insert new record in left node
		{
			u_int16_t	leftInsertIndex;
			
			leftInsertIndex = leftNode->numRecords;

			didItFit = InsertKeyRecord (btreePtr, leftNode, leftInsertIndex,
										keyPtr, keyLength, recPtr, recSize);
			if ( !didItFit )
			{
				Panic ("RotateLeft: InsertKeyRecord (left) returned false!");
				err = fsBTBadRotateErr;
				goto ErrorExit;
			}
			
			*insertIndex = leftInsertIndex;
			*insertNodeNum = rightNode->bLink;
		}
		else
		{
			didItFit = RotateRecordLeft (btreePtr, leftNode, rightNode);
			if ( !didItFit )
			{
				Panic ("RotateLeft: RotateRecordLeft returned false!");
				err = fsBTBadRotateErr;
				goto ErrorExit;
			}
		}
		
		++index;
	}
	
	if ( moveIndex <= rightInsertIndex )	// then insert new record in right node
	{
		rightInsertIndex -= index;			// adjust for records already rotated
		
		didItFit = InsertKeyRecord (btreePtr, rightNode, rightInsertIndex,
									keyPtr, keyLength, recPtr, recSize);
		if ( !didItFit )
		{
			Panic ("RotateLeft: InsertKeyRecord (right) returned false!");
			err = fsBTBadRotateErr;
			goto ErrorExit;
		}
	
		*insertIndex = rightInsertIndex;
		*insertNodeNum = leftNode->fLink;
	}


	return noErr;


	////////////////////////////// Error Exit ///////////////////////////////////

ErrorExit:

	*insertIndex	= 0;
	*insertNodeNum	= 0;
	*recordFit		= false;
	*recsRotated	= 0;
	
	return	err;
}



/////////////////////////////////// SplitLeft ///////////////////////////////////

static OSStatus	SplitLeft		(BTreeControlBlockPtr		 btreePtr,
								 BlockDescriptor			*leftNode,
								 BlockDescriptor			*rightNode,
								 u_int32_t					 rightNodeNum,
								 u_int16_t					 index,
								 KeyPtr						 keyPtr,
								 u_int8_t *					 recPtr,
								 u_int16_t					 recSize,
								 u_int16_t					*insertIndex,
								 u_int32_t					*insertNodeNum,
								 u_int16_t					*recsRotated )
{
	OSStatus			err;
	NodeDescPtr			left, right;
	u_int32_t			newNodeNum;
	Boolean				recordFit;
	
	
	///////////////////////////// Compare Nodes /////////////////////////////////

	right = rightNode->buffer;
	left  = leftNode->buffer;
	
	PanicIf ( right->bLink != 0 && left == 0, " SplitLeft: left sibling missing!?" );
	
	/* type should be kBTLeafNode or kBTIndexNode */
	
	if ( (right->height == 1) && (right->kind != kBTLeafNode) )
		return	fsBTInvalidNodeErr;
	
	if ( left != nil )
	{
		if ( left->fLink != rightNodeNum )
			return fsBTInvalidNodeErr;										//�� E_BadSibling ?
	
		if ( left->height != right->height )
			return	fsBTInvalidNodeErr;										//�� E_BadNodeHeight ?
		
		if ( left->kind != right->kind )
			return	fsBTInvalidNodeErr;										//�� E_BadNodeType ?
	}
	

	///////////////////////////// Allocate Node /////////////////////////////////

	err = AllocateNode (btreePtr, &newNodeNum);
	M_ExitOnError (err);
	

	/////////////// Update Forward Link In Original Left Node ///////////////////

	if ( left != nil )
	{
		// XXXdbg
		ModifyBlockStart(btreePtr->fileRefNum, leftNode);

		left->fLink	= newNodeNum;
		err = UpdateNode (btreePtr, leftNode, 0, kLockTransaction);
		M_ExitOnError (err);
	}


	/////////////////////// Initialize New Left Node ////////////////////////////

	err = GetNewNode (btreePtr, newNodeNum, leftNode);
	M_ExitOnError (err);
	
	// XXXdbg
	ModifyBlockStart(btreePtr->fileRefNum, leftNode);

	left		= leftNode->buffer;
	left->fLink	= rightNodeNum;
	

	// Steal Info From Right Node
	
	left->bLink  = right->bLink;
	left->kind   = right->kind;
	left->height = right->height;
	
	right->bLink		= newNodeNum;			// update Right bLink

	if ( (left->kind == kBTLeafNode) && (left->bLink == 0) )
	{
		// if we're adding a new first leaf node - update BTreeInfoRec
		
		btreePtr->firstLeafNode = newNodeNum;
		M_BTreeHeaderDirty (btreePtr);		//�� AllocateNode should have set the bit already...
	}

	////////////////////////////// Rotate Left //////////////////////////////////

	err = RotateLeft (btreePtr, left, right, index, keyPtr, recPtr, recSize,
					  insertIndex, insertNodeNum, &recordFit, recsRotated);
	
	M_ExitOnError (err);

	return noErr;
	
ErrorExit:
	
	(void) ReleaseNode (btreePtr, leftNode);
	(void) ReleaseNode (btreePtr, rightNode);
	
	//�� Free new node if allocated?

	*insertIndex	= 0;
	*insertNodeNum	= 0;
	*recsRotated	= 0;
	
	return	err;
}



/////////////////////////////// RotateRecordLeft ////////////////////////////////

static Boolean RotateRecordLeft (BTreeControlBlockPtr		btreePtr,
								 NodeDescPtr				leftNode,
							 	 NodeDescPtr				rightNode )
{
	u_int16_t	size;
	u_int8_t *	recPtr;
	Boolean		recordFit;
	
	size	= GetRecordSize (btreePtr, rightNode, 0);
	recPtr	= GetRecordAddress (btreePtr, rightNode, 0);
	
	recordFit = InsertRecord (btreePtr, leftNode, leftNode->numRecords, recPtr, size);
	
	if ( !recordFit )
		return false;
	
	DeleteRecord (btreePtr, rightNode, 0);
	
	return true;
}


//////////////////////////////// AddNewRootNode /////////////////////////////////

static OSStatus	AddNewRootNode	(BTreeControlBlockPtr	 btreePtr,
								 NodeDescPtr			 leftNode,
								 NodeDescPtr			 rightNode )
{
	OSStatus			err;
	BlockDescriptor		rootNode;
	u_int32_t			rootNum;
	KeyPtr				keyPtr;
	Boolean				didItFit;
	u_int16_t			keyLength;	
	
	rootNode.buffer = nil;
	rootNode.blockHeader = nil;

	PanicIf (leftNode == nil, "AddNewRootNode: leftNode == nil");
	PanicIf (rightNode == nil, "AddNewRootNode: rightNode == nil");
	
	
	/////////////////////// Initialize New Root Node ////////////////////////////
	
	err = AllocateNode (btreePtr, &rootNum);
	M_ExitOnError (err);
	
	err = GetNewNode (btreePtr, rootNum, &rootNode);
	M_ExitOnError (err);
		
	// XXXdbg
	ModifyBlockStart(btreePtr->fileRefNum, &rootNode);

	((NodeDescPtr)rootNode.buffer)->kind = kBTIndexNode;
	((NodeDescPtr)rootNode.buffer)->height	= ++btreePtr->treeDepth;
	

	///////////////////// Insert Left Node Index Record /////////////////////////	

	keyPtr = (KeyPtr) GetRecordAddress (btreePtr, leftNode, 0);
	keyLength = GetKeyLength(btreePtr, keyPtr, false);

	didItFit = InsertKeyRecord ( btreePtr, rootNode.buffer, 0, keyPtr, keyLength,
								 (u_int8_t *) &rightNode->bLink, 4 );

	PanicIf ( !didItFit, "AddNewRootNode:InsertKeyRecord failed for left index record");


	//////////////////// Insert Right Node Index Record /////////////////////////

	keyPtr = (KeyPtr) GetRecordAddress (btreePtr, rightNode, 0);
	keyLength = GetKeyLength(btreePtr, keyPtr, false);

	didItFit = InsertKeyRecord ( btreePtr, rootNode.buffer, 1, keyPtr, keyLength,
								 (u_int8_t *) &leftNode->fLink, 4 );

	PanicIf ( !didItFit, "AddNewRootNode:InsertKeyRecord failed for right index record");

	
	/////////////////////////// Release Root Node ///////////////////////////////
	
	err = UpdateNode (btreePtr, &rootNode, 0, kLockTransaction);
	M_ExitOnError (err);
	
	// update BTreeInfoRec
	
	btreePtr->rootNode	 = rootNum;
	M_BTreeHeaderDirty(btreePtr);

	return noErr;


	////////////////////////////// Error Exit ///////////////////////////////////

ErrorExit:

	return	err;
}


static u_int16_t	GetKeyLength ( const BTreeControlBlock *btreePtr, const BTreeKey *key, Boolean forLeafNode )
{
	u_int16_t length;

	if ( forLeafNode || btreePtr->attributes & kBTVariableIndexKeysMask )
		length = KeyLength (btreePtr, key);		// just use actual key length
	else
		length = btreePtr->maxKeyLength;		// fixed sized index key (i.e. HFS)		//�� shouldn't we clear the pad bytes?

	return length;
}

