/*
 * Copyright (c) 1996-2015 Apple Inc. All rights reserved.
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
 *
 *	@(#)BTreeScanner.c
 */
#include <sys/kernel.h>
#include "hfs_endian.h"

#include "BTreeScanner.h"

static int FindNextLeafNode(	BTScanState *scanState, Boolean avoidIO );
static int ReadMultipleNodes( 	BTScanState *scanState );


//_________________________________________________________________________________
//
//	Routine:	BTScanNextRecord
//
//	Purpose:	Return the next leaf record in a scan.
//
//	Inputs:
//		scanState		Scanner's current state
//		avoidIO			If true, don't do any I/O to refill the buffer
//
//	Outputs:
//		key				Key of found record (points into buffer)
//		data			Data of found record (points into buffer)
//		dataSize		Size of data in found record
//
//	Result:
//		noErr			Found a valid record
//		btNotFound		No more records
//		???				Needed to do I/O to get next node, but avoidIO set
//
//	Notes:
//		This routine returns pointers to the found record's key and data.  It
//		does not copy the key or data to a caller-supplied buffer (like
//		GetBTreeRecord would).  The caller must not modify the key or data.
//_________________________________________________________________________________

int BTScanNextRecord(	BTScanState *	scanState,
						Boolean			avoidIO,
						void * *		key,
						void * *		data,
						u_int32_t *		dataSize  )
{
	int				err;
	u_int16_t		dataSizeShort;
	
	err = noErr;

	//
	//	If this is the first call, there won't be any nodes in the buffer, so go
	//	find the first first leaf node (if any).
	//	
	if ( scanState->nodesLeftInBuffer == 0 )
	{
		err = FindNextLeafNode( scanState, avoidIO );
	}

	while ( err == noErr ) 
	{ 
		//	See if we have a record in the current node
		err = GetRecordByIndex( scanState->btcb, scanState->currentNodePtr, 
								scanState->recordNum, (KeyPtr *) key, 
								(u_int8_t **) data, &dataSizeShort  );

		if ( err == noErr )
		{
			++scanState->recordsFound;
			++scanState->recordNum;
			if (dataSize != NULL)
				*dataSize = dataSizeShort;
			return noErr;
		}
		else if (err > 0)
		{
			//	We didn't get the node through the cache, so we can't invalidate it.
			//XXX Should we do something else to avoid seeing the same record again?
			return err;
		}
		
		//	We're done with the current node.  See if we've returned all the records
		if ( scanState->recordsFound >= scanState->btcb->leafRecords )
		{
			return btNotFound;
		}

		//	Move to the first record of the next leaf node
		scanState->recordNum = 0; 
		err = FindNextLeafNode( scanState, avoidIO );
	}
	
	//
	//	If we got an EOF error from FindNextLeafNode, then there are no more leaf
	//	records to be found.
	//
	if ( err == fsEndOfIterationErr )
		err = btNotFound;
	
	return err;
	
} /* BTScanNextRecord */


//_________________________________________________________________________________
//
//	Routine:	FindNextLeafNode
//
//	Purpose:	Point to the next leaf node in the buffer.  Read more nodes
//				into the buffer if needed (and allowed).
//
//	Inputs:
//		scanState		Scanner's current state
//		avoidIO			If true, don't do any I/O to refill the buffer
//
//	Result:
//		noErr			Found a valid record
//		fsEndOfIterationErr	No more nodes in file
//		???				Needed to do I/O to get next node, but avoidIO set
//_________________________________________________________________________________

static int FindNextLeafNode(	BTScanState *scanState, Boolean avoidIO )
{
	int err;
	BlockDescriptor block;
	FileReference fref;
	
	err = noErr;		// Assume everything will be OK
	
	while ( 1 ) 
	{
		if ( scanState->nodesLeftInBuffer == 0 ) 
		{
			//	Time to read some more nodes into the buffer
			if ( avoidIO ) 
			{
				return fsBTTimeOutErr;
			}
			else 
			{
				//	read some more nodes into buffer
				err = ReadMultipleNodes( scanState );
				if ( err != noErr ) 
					break;
			}
		}
		else 
		{
			//	Adjust the node counters and point to the next node in the buffer
			++scanState->nodeNum;
			--scanState->nodesLeftInBuffer;
			
			//	If we've looked at all nodes in the tree, then we're done
			if ( scanState->nodeNum >= scanState->btcb->totalNodes )
				return fsEndOfIterationErr;

			if ( scanState->nodesLeftInBuffer == 0 )
			{
				scanState->recordNum = 0; 
				continue; 
			}

			scanState->currentNodePtr = (BTNodeDescriptor *)(((u_int8_t *)scanState->currentNodePtr) 
										+ scanState->btcb->nodeSize);
		}
		
		/* Fake a BlockDescriptor */
		block.blockHeader = NULL;	/* No buffer cache buffer */
		block.buffer = scanState->currentNodePtr;
		block.blockNum = scanState->nodeNum;
		block.blockSize = scanState->btcb->nodeSize;
		block.blockReadFromDisk = 1;
		block.isModified = 0;
		
		fref = scanState->btcb->fileRefNum;
		
		/* This node was read from disk, so it must be swapped/checked.
		 * Since we are reading multiple nodes, we might have read an 
		 * unused node.  Therefore we allow swapping of unused nodes.
		 */
		err = hfs_swap_BTNode(&block, fref, kSwapBTNodeBigToHost, true);
		if ( err != noErr ) {
			printf("hfs: FindNextLeafNode: Error from hfs_swap_BTNode (node %u)\n", scanState->nodeNum);
			continue;
		}

		if ( scanState->currentNodePtr->kind == kBTLeafNode )
			break;
	}
	
	return err;
	
} /* FindNextLeafNode */


//_________________________________________________________________________________
//
//	Routine:	ReadMultipleNodes
//
//	Purpose:	Read one or more nodes into the buffer.
//
//	Inputs:
//		theScanStatePtr		Scanner's current state
//
//	Result:
//		noErr				One or nodes were read
//		fsEndOfIterationErr		No nodes left in file, none in buffer
//_________________________________________________________________________________

static int ReadMultipleNodes( BTScanState *theScanStatePtr )
{
	int						myErr = E_NONE;
	BTreeControlBlockPtr  	myBTreeCBPtr;
	daddr64_t				myPhyBlockNum;
	u_int32_t				myBufferSize;
	struct vnode *			myDevPtr;
	unsigned int			myBlockRun;
	u_int32_t				myBlocksInBufferCount;

	// release old buffer if we have one
	if ( theScanStatePtr->bufferPtr != NULL )
	{
	        buf_markinvalid(theScanStatePtr->bufferPtr);
		buf_brelse( theScanStatePtr->bufferPtr );
		theScanStatePtr->bufferPtr = NULL;
		theScanStatePtr->currentNodePtr = NULL;
	}
	
	myBTreeCBPtr = theScanStatePtr->btcb;
			
	// map logical block in catalog btree file to physical block on volume
	myErr = hfs_bmap(myBTreeCBPtr->fileRefNum, theScanStatePtr->nodeNum, 
	                 &myDevPtr, &myPhyBlockNum, &myBlockRun);
	if ( myErr != E_NONE )
	{
		goto ExitThisRoutine;
	}

	// bmap block run gives us the remaining number of valid blocks (number of blocks 
	// minus the first).  so if there are 10 valid blocks our run number will be 9.
	// blocks, in our case is the same as nodes (both are 4K)
	myBlocksInBufferCount = (theScanStatePtr->bufferSize / myBTreeCBPtr->nodeSize );
	myBufferSize = theScanStatePtr->bufferSize;
	if ( (myBlockRun + 1) < myBlocksInBufferCount )
	{
		myBufferSize = (myBlockRun + 1) * myBTreeCBPtr->nodeSize;
	}
	
	// now read blocks from the device 
	myErr = (int)buf_meta_bread(myDevPtr, 
	                       myPhyBlockNum, 
	                       myBufferSize,  
	                       NOCRED, 
	                       &theScanStatePtr->bufferPtr );
	if ( myErr != E_NONE )
	{
		goto ExitThisRoutine;
	}

	theScanStatePtr->nodesLeftInBuffer = buf_count(theScanStatePtr->bufferPtr) / theScanStatePtr->btcb->nodeSize;
	theScanStatePtr->currentNodePtr = (BTNodeDescriptor *) buf_dataptr(theScanStatePtr->bufferPtr);

ExitThisRoutine:
	return myErr;
	
} /* ReadMultipleNodes */



//_________________________________________________________________________________
//
//	Routine:	BTScanInitialize
//
//	Purpose:	Prepare to start a new BTree scan, or resume a previous one.
//
//	Inputs:
//		btreeFile		The B-Tree's file control block
//		startingNode	Initial node number
//		startingRecord	Initial record number within node
//		recordsFound	Number of valid records found so far
//		bufferSize		Size (in bytes) of buffer
//
//	Outputs:
//		scanState		Scanner's current state; pass to other scanner calls
//
//	Notes:
//		To begin a new scan and see all records in the B-Tree, pass zeroes for
//		startingNode, startingRecord, and recordsFound.
//
//		To resume a scan from the point of a previous BTScanTerminate, use the
//		values returned by BTScanTerminate as input for startingNode, startingRecord,
//		and recordsFound.
//
//		When resuming a scan, the caller should check the B-tree's write count.  If
//		it is different from the write count when the scan was terminated, then the
//		tree may have changed and the current state may be incorrect.  In particular,
//		you may see some records more than once, or never see some records.  Also,
//		the scanner may not be able to detect when all leaf records have been seen,
//		and will have to scan through many empty nodes.
//
//		XXX�Perhaps the write count should be managed by BTScanInitialize and
//		XXX BTScanTerminate?  This would avoid the caller having to peek at
//		XXX internal B-Tree structures.
//_________________________________________________________________________________

int		BTScanInitialize(	const FCB *		btreeFile,
							u_int32_t		startingNode,
							u_int32_t		startingRecord,
							u_int32_t		recordsFound,
							u_int32_t		bufferSize,
							BTScanState	*	scanState     )
{
	BTreeControlBlock	*btcb;
	
	//
	//	Make sure this is a valid B-Tree file
	//
	btcb = (BTreeControlBlock *) btreeFile->fcbBTCBPtr;
	if (btcb == NULL)
		return fsBTInvalidFileErr;
	
	//
	//	Make sure buffer size is big enough, and a multiple of the
	//	B-Tree node size
	//
	if ( bufferSize < btcb->nodeSize )
		return paramErr;
	bufferSize = (bufferSize / btcb->nodeSize) * btcb->nodeSize;

	//
	//	Set up the scanner's state
	//
	scanState->bufferSize			= bufferSize;
	scanState->bufferPtr 			= NULL;
	scanState->btcb					= btcb;
	scanState->nodeNum				= startingNode;
	scanState->recordNum			= startingRecord;
	scanState->currentNodePtr		= NULL;
	scanState->nodesLeftInBuffer	= 0;		// no nodes currently in buffer
	scanState->recordsFound			= recordsFound;
	microuptime(&scanState->startTime);			// initialize our throttle
		
	return noErr;
	
} /* BTScanInitialize */


//_________________________________________________________________________________
//
//	Routine:	BTScanTerminate
//
//	Purpose:	Return state information about a scan so that it can be resumed
//				later via BTScanInitialize.
//
//	Inputs:
//		scanState		Scanner's current state
//
//	Outputs:
//		nextNode		Node number to resume a scan (pass to BTScanInitialize)
//		nextRecord		Record number to resume a scan (pass to BTScanInitialize)
//		recordsFound	Valid records seen so far (pass to BTScanInitialize)
//_________________________________________________________________________________

int	 BTScanTerminate(	BTScanState *		scanState,
						u_int32_t *			startingNode,
						u_int32_t *			startingRecord,
						u_int32_t *			recordsFound	)
{
	*startingNode	= scanState->nodeNum;
	*startingRecord	= scanState->recordNum;
	*recordsFound	= scanState->recordsFound;

	if ( scanState->bufferPtr != NULL )
	{
		buf_markinvalid(scanState->bufferPtr);
		buf_brelse( scanState->bufferPtr );
		scanState->bufferPtr = NULL;
		scanState->currentNodePtr = NULL;
	}
	
	return noErr;
	
} /* BTScanTerminate */


