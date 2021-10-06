/*
 * Copyright (c) 1999-2001 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 Copyright (c) 1987-99 Apple Computer, Inc.
 All Rights Reserved.

 About hfs.util.m:
 Contains code to implement hfs utility used by the WorkSpace to mount HFS.

 Change History:
     5-Jan-1999 Don Brady       Write hfs.label names in UTF-8.
    10-Dec-1998 Pat Dirks       Changed to try built-in hfs filesystem first.
     3-Sep-1998	Don Brady	Disable the daylight savings time stuff.
    28-Aug-1998 chw		Fixed parse args and verify args to indicate that the
			        flags (fixed or removable) are required in the probe case.
    22-Jun-1998	Pat Dirks	Changed HFSToUFSStr table to switch ":" and "/".
    13-Jan-1998 jwc 		first cut (derived from old NextStep macfs.util code and cdrom.util code).
 */


/* ************************************** I N C L U D E S ***************************************** */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/vmmeter.h>
#include <sys/mount.h>
#include <sys/wait.h>

#include <dev/disk.h>
#include <sys/loadable_fs.h>
#include <hfs/hfs_format.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <CoreFoundation/CFString.h>

#define READ_DEFAULT_ENCODING 1

#ifndef FSUR_MOUNT_HIDDEN
#define FSUR_MOUNT_HIDDEN (-9)
#endif

#ifndef FSUC_ADOPT
#define FSUC_ADOPT 'a'
#endif

#ifndef FSUC_DISOWN
#define FSUC_DISOWN 'd'
#endif

#ifndef FSUC_GETUUID
#define FSUC_GETUUID 'k'
#endif

#ifndef FSUC_SETUUID
#define FSUC_SETUUID 's'
#endif

#ifndef FSUC_MKJNL
#define FSUC_MKJNL   'J'
#endif

#ifndef FSUC_UNJNL
#define FSUC_UNJNL   'U'
#endif


/* **************************************** L O C A L S ******************************************* */

#define	HFS_BLOCK_SIZE			512

char gHFS_FS_NAME[] = "hfs";
char gHFS_FS_NAME_NAME[] = "HFS";

char gNewlineString[] = "\n";

char gMountCommand[] = "/sbin/mount";

char gUnmountCommand[] = "/sbin/umount";

char gReadOnlyOption[] = "-r";
char gReadWriteOption[] = "-w";

char gSuidOption[] = "suid";
char gNoSuidOption[] = "nosuid";

char gDevOption[] = "dev";
char gNoDevOption[] = "nodev";

char gUsePermissionsOption[] = "perm";
char gIgnorePermissionsOption[] = "noperm";

boolean_t gIsEjectable = 0;

int gJournalSize = 0;

#define AUTO_ADOPT_FIXED 1
#define AUTO_ENTER_FIXED 0


#define VOLUMEUUIDVALUESIZE 2
typedef union VolumeUUID {
	unsigned long value[VOLUMEUUIDVALUESIZE];
	struct {
		unsigned long high;
		unsigned long low;
	} v;
} VolumeUUID;

#define VOLUMEUUIDLENGTH 16
typedef char VolumeUUIDString[VOLUMEUUIDLENGTH+1];

#define VOLUME_RECORDED 0x80000000
#define VOLUME_USEPERMISSIONS 0x00000001
#define VOLUME_VALIDSTATUSBITS ( VOLUME_USEPERMISSIONS )

typedef void *VolumeStatusDBHandle;

void GenerateVolumeUUID(VolumeUUID *newVolumeID);
void ConvertVolumeUUIDStringToUUID(const char *UUIDString, VolumeUUID *volumeID);
void ConvertVolumeUUIDToString(VolumeUUID *volumeID, char *UUIDString);
int OpenVolumeStatusDB(VolumeStatusDBHandle *DBHandlePtr);
int GetVolumeStatusDBEntry(VolumeStatusDBHandle DBHandle, VolumeUUID *volumeID, unsigned long *VolumeStatus);
int SetVolumeStatusDBEntry(VolumeStatusDBHandle DBHandle, VolumeUUID *volumeID, unsigned long VolumeStatus);
int DeleteVolumeStatusDBEntry(VolumeStatusDBHandle DBHandle, VolumeUUID *volumeID);
int CloseVolumeStatusDB(VolumeStatusDBHandle DBHandle);

/* ************************************ P R O T O T Y P E S *************************************** */
static void	DoDisplayUsage( const char * argv[] );
static void	DoFileSystemFile( char * theFileNameSuffixPtr, char * theContentsPtr );
static int	DoMount( char * theDeviceNamePtr, const char * theMountPointPtr, boolean_t isLocked, boolean_t isSetuid, boolean_t isDev );
static int 	DoProbe( char * theDeviceNamePtr );
static int 	DoUnmount( const char * theMountPointPtr );
static int	DoGetUUIDKey( const char * theDeviceNamePtr );
static int	DoChangeUUIDKey( const char * theDeviceNamePtr );
static int	DoAdopt( const char * theDeviceNamePtr );
static int	DoDisown( const char * theDeviceNamePtr );

extern int  DoMakeJournaled( const char * volNamePtr, int journalSize );  // XXXdbg
extern int  DoUnJournal( const char * volNamePtr );      // XXXdbg

static int	ParseArgs( int argc, const char * argv[], const char ** actionPtr, const char ** mountPointPtr, boolean_t * isEjectablePtr, boolean_t * isLockedPtr, boolean_t * isSetuidPtr, boolean_t * isDevPtr );

static int	GetVolumeUUID(const char *deviceNamePtr, VolumeUUID *volumeUUIDPtr, boolean_t generate);
static int	SetVolumeUUID(const char *deviceNamePtr, VolumeUUID *volumeUUIDPtr);
static int	GetEmbeddedHFSPlusVol(HFSMasterDirectoryBlock * hfsMasterDirectoryBlockPtr, off_t * startOffsetPtr);
static int	GetNameFromHFSPlusVolumeStartingAt(int fd, off_t hfsPlusVolumeOffset, char * name_o);
static int	GetBTreeNodeInfo(int fd, off_t hfsPlusVolumeOffset, u_int32_t blockSize,
							u_int32_t extentCount, const HFSPlusExtentDescriptor *extentList,
							u_int32_t *nodeSize, u_int32_t *firstLeafNode);
static int	GetCatalogOverflowExtents(int fd, off_t hfsPlusVolumeOffset, HFSPlusVolumeHeader *volHdrPtr,
									 HFSPlusExtentDescriptor **catalogExtents, u_int32_t *catalogExtCount);
static int	LogicalToPhysical(off_t logicalOffset, ssize_t length, u_int32_t blockSize,
							u_int32_t extentCount, const HFSPlusExtentDescriptor *extentList,
							off_t *physicalOffset, ssize_t *availableBytes);
static int	ReadFile(int fd, void *buffer, off_t offset, ssize_t length,
					off_t volOffset, u_int32_t blockSize,
					u_int32_t extentCount, const HFSPlusExtentDescriptor *extentList);
static ssize_t	readAt( int fd, void * buf, off_t offset, ssize_t length );
static ssize_t	writeAt( int fd, void * buf, off_t offset, ssize_t length );


/*
 * The fuction CFStringGetSystemEncoding does not work correctly in
 * our context (autodiskmount deamon).  We include a local copy here
 * so that we can derive the default encoding. Radar 2516316.
 */
#if READ_DEFAULT_ENCODING
#define __kCFUserEncodingFileName ("/.CFUserTextEncoding")

static unsigned int __CFStringGetDefaultEncodingForHFSUtil() {
    struct passwd *passwdp;

    if ((passwdp = getpwuid(0))) { // root account
        char buffer[MAXPATHLEN + 1];
        int fd;

        strcpy(buffer, passwdp->pw_dir);
        strcat(buffer, __kCFUserEncodingFileName);

        if ((fd = open(buffer, O_RDONLY, 0)) > 0) {
            size_t readSize;

            readSize = read(fd, buffer, MAXPATHLEN);
            buffer[(readSize < 0 ? 0 : readSize)] = '\0';
            close(fd);
            return strtol(buffer, NULL, 0);
        }
    }
    return 0; // Fallback to smRoman
}
#endif

/* ******************************************** main ************************************************
Purpose -
This our main entry point to this utility.  We get called by the WorkSpace.  See ParseArgs
for detail info on input arguments.
Input -
argc - the number of arguments in argv.
argv - array of arguments.
Output -
returns FSUR_IO_SUCCESS if OK else one of the other FSUR_xyz errors in loadable_fs.h.
*************************************************************************************************** */

int main (int argc, const char *argv[])
{
    const char			*	actionPtr = NULL;
    char				rawDeviceName[MAXPATHLEN];
    char				blockDeviceName[MAXPATHLEN];
    const char			*	mountPointPtr = NULL;
    int						result = FSUR_IO_SUCCESS;
    boolean_t				isLocked = 0;	/* reasonable assumptions */
    boolean_t				isSetuid = 0;	/* reasonable assumptions */
    boolean_t				isDev = 0;	/* reasonable assumptions */

    /* Verify our arguments */
    if ( (result = ParseArgs( argc, argv, & actionPtr, & mountPointPtr, & gIsEjectable, & isLocked, &isSetuid, &isDev )) != 0 ) {
        goto AllDone;
    }

    /*
    -- Build our device name (full path), should end up with something like:
    --   "/dev/disk0s2"
    */

    sprintf(rawDeviceName, "/dev/r%s", argv[2]);
    sprintf(blockDeviceName, "/dev/%s", argv[2]);

    /* call the appropriate routine to handle the given action argument after becoming root */

    result = seteuid( 0 );
    if ( result ) {
		fprintf(stderr, "You must be root to run %s.\n", argv[0]);
        result = FSUR_INVAL;
        goto AllDone;
    }

    result = setegid( 0 );	// PPD - is this necessary?

    switch( * actionPtr ) {
        case FSUC_PROBE:
            result = DoProbe(rawDeviceName);
            break;

        case FSUC_MOUNT:
        case FSUC_MOUNT_FORCE:
            result = DoMount(blockDeviceName, mountPointPtr, isLocked, isSetuid, isDev);
            break;

        case FSUC_UNMOUNT:
            result = DoUnmount( mountPointPtr );
            break;
		case FSUC_GETUUID:
			result = DoGetUUIDKey( blockDeviceName );
			break;
		
		case FSUC_SETUUID:
			result = DoChangeUUIDKey( blockDeviceName );
			break;
		case FSUC_ADOPT:
			result = DoAdopt( blockDeviceName );
			break;
		
		case FSUC_DISOWN:
			result = DoDisown( blockDeviceName );
			break;

		case FSUC_MKJNL:
			if (gJournalSize) {
				result = DoMakeJournaled( argv[3], gJournalSize );
			} else {
				result = DoMakeJournaled( argv[2], gJournalSize );
			}
			break;

		case FSUC_UNJNL:
			result = DoUnJournal( argv[2] );
			break;
			
        default:
            /* should never get here since ParseArgs should handle this situation */
            DoDisplayUsage( argv );
            result = FSUR_INVAL;
            break;
    }

AllDone:

    exit(result);

    return result;	/*...and make main fit the ANSI spec. */
}


/* ***************************** DoMount ********************************
Purpose -
This routine will fire off a system command to mount the given device at the given mountpoint.
autodiskmount will make sure the mountpoint exists and will remove it at Unmount time.
Input -
deviceNamePtr - pointer to the device name (full path, like /dev/disk0s2).
mountPointPtr - pointer to the mount point.
isLocked - a flag
Output -
returns FSUR_IO_SUCCESS everything is cool else one of several other FSUR_xyz error codes.
*********************************************************************** */
static int
DoMount(char *deviceNamePtr, const char *mountPointPtr, boolean_t isLocked, boolean_t isSetuid, boolean_t isDev)
{
	int pid;
        char *isLockedstr;
        char *isSetuidstr;
        char *isDevstr;
	char *permissionsOption;
	int result = FSUR_IO_FAIL;
	union wait status;
	char encodeopt[16] = "";
	CFStringEncoding encoding;
	VolumeUUID targetVolumeUUID;
	VolumeStatusDBHandle vsdbhandle = NULL;
	unsigned long targetVolumeStatus;

	if (mountPointPtr == NULL || *mountPointPtr == '\0')
		return (FSUR_IO_FAIL);

	/* get the volume UUID to check if permissions should be used: */
	targetVolumeStatus = 0;
	if (((result = GetVolumeUUID(deviceNamePtr, &targetVolumeUUID, FALSE)) != FSUR_IO_SUCCESS) ||
		(targetVolumeUUID.v.high ==0) ||
		(targetVolumeUUID.v.low == 0)) {
#if TRACE_HFS_UTIL
		fprintf(stderr, "hfs.util: DoMount: GetVolumeUUID returned %d.\n", result);
#endif
#if AUTO_ADOPT_FIXED
		if (gIsEjectable == 0) {
			result = DoAdopt( deviceNamePtr );
#if TRACE_HFS_UTIL
			fprintf(stderr, "hfs.util: DoMount: Auto-adopting %s; result = %d.\n", deviceNamePtr, result);
#endif
			targetVolumeStatus = VOLUME_USEPERMISSIONS;
		} else {
#if TRACE_HFS_UTIL
			fprintf(stderr, "hfs.util: DoMount: Not adopting ejectable %s.\n", deviceNamePtr);
#endif
			targetVolumeStatus = 0;
		};
#endif
	} else {
		/* We've got a real volume UUID! */
#if TRACE_HFS_UTIL
		fprintf(stderr, "hfs.util: DoMount: UUID = %08lX%08lX.\n", targetVolumeUUID.v.high, targetVolumeUUID.v.low);
#endif
		if ((result = OpenVolumeStatusDB(&vsdbhandle)) != 0) {
			/* Can't even get access to the volume info db; assume permissions are OK. */
#if TRACE_HFS_UTIL
			fprintf(stderr, "hfs.util: DoMount: OpenVolumeStatusDB returned %d; ignoring permissions.\n", result);
#endif
			targetVolumeStatus = VOLUME_USEPERMISSIONS;
		} else {
#if TRACE_HFS_UTIL
			fprintf(stderr, "hfs.util: DoMount: Looking up volume status...\n");
#endif
			if ((result = GetVolumeStatusDBEntry(vsdbhandle, &targetVolumeUUID, &targetVolumeStatus)) != 0) {
#if TRACE_HFS_UTIL
				fprintf(stderr, "hfs.util: DoMount: GetVolumeStatusDBEntry returned %d.\n", result);
#endif
#if AUTO_ENTER_FIXED
				if (gIsEjectable == 0) {
					result = DoAdopt( deviceNamePtr );
#if TRACE_HFS_UTIL
					fprintf(stderr, "hfs.util: DoMount: Auto-adopting %s; result = %d.\n", deviceNamePtr, result);
#endif
					targetVolumeStatus = VOLUME_USEPERMISSIONS;
				} else {
#if TRACE_HFS_UTIL
					fprintf(stderr, "hfs.util: DoMount: Not adopting ejectable %s.\n", deviceNamePtr);
#endif
					targetVolumeStatus = 0;
				};
#else
				targetVolumeStatus = 0;
#endif
			};
			(void)CloseVolumeStatusDB(vsdbhandle);
			vsdbhandle = NULL;
		};
	};
	
	pid = fork();
	if (pid == 0) {
                isLockedstr = isLocked ? gReadOnlyOption : gReadWriteOption;
                isSetuidstr = isSetuid ? gSuidOption : gNoSuidOption;
                isDevstr = isDev ? gDevOption : gNoDevOption;
		
		permissionsOption =
			(targetVolumeStatus & VOLUME_USEPERMISSIONS) ? gUsePermissionsOption : gIgnorePermissionsOption;

		/* get default encoding value (for hfs volumes) */
#if READ_DEFAULT_ENCODING
		encoding = __CFStringGetDefaultEncodingForHFSUtil();
#else
		encoding = CFStringGetSystemEncoding();
#endif
		sprintf(encodeopt, "-e=%d", (int)encoding);
#if TRACE_HFS_UTIL
		fprintf(stderr, "hfs.util: %s %s -o -x -o %s -o %s -o -u=unknown,-g=unknown,-m=0777 -t %s %s %s ...\n",
							gMountCommand, isLockedstr, encodeopt, permissionsOption, gHFS_FS_NAME, deviceNamePtr, mountPointPtr);
#endif
                (void) execl(gMountCommand, gMountCommand, isLockedstr, "-o", isSetuidstr, "-o", isDevstr,
                                        "-o", encodeopt, "-o", permissionsOption,
                                        "-o", "-u=unknown,-g=unknown,-m=0777",
                                        "-t", gHFS_FS_NAME, deviceNamePtr, mountPointPtr, NULL);
                

		/* IF WE ARE HERE, WE WERE UNSUCCESFULL */
		return (FSUR_IO_FAIL);
	}

	if (pid == -1)
		return (FSUR_IO_FAIL);

	/* Success! */
	if ((wait4(pid, (int *)&status, 0, NULL) == pid) && (WIFEXITED(status)))
		result = status.w_retcode;
	else
		result = -1;

	return (result == 0) ? FSUR_IO_SUCCESS : FSUR_IO_FAIL;
}


/* ****************************************** DoUnmount *********************************************
Purpose -
    This routine will fire off a system command to unmount the given device.
Input -
    theDeviceNamePtr - pointer to the device name (full path, like /dev/disk0s2).
Output -
    returns FSUR_IO_SUCCESS everything is cool else FSUR_IO_FAIL.
*************************************************************************************************** */
static int
DoUnmount(const char * theMountPointPtr)
{
	int pid;
	union wait status;
	int result;

	if (theMountPointPtr == NULL || *theMountPointPtr == '\0') return (FSUR_IO_FAIL);

	pid = fork();
	if (pid == 0) {
#if TRACE_HFS_UTIL
		fprintf(stderr, "hfs.util: %s %s ...\n", gUnmountCommand, theMountPointPtr);
#endif
		(void) execl(gUnmountCommand, gUnmountCommand, theMountPointPtr, NULL);

		/* IF WE ARE HERE, WE WERE UNSUCCESFULL */
		return (FSUR_IO_FAIL);
	}

	if (pid == -1)
		return (FSUR_IO_FAIL);

	/* Success! */
	if ((wait4(pid, (int *)&status, 0, NULL) == pid) && (WIFEXITED(status)))
		result = status.w_retcode;
	else
		result = -1;

	return (result == 0 ? FSUR_IO_SUCCESS : FSUR_IO_FAIL);

} /* DoUnmount */


/* ******************************************* DoProbe **********************************************
Purpose -
    This routine will open the given raw device and check to make sure there is media that looks
    like an HFS.
Input -
    theDeviceNamePtr - pointer to the device name (full path, like /dev/disk0s2).
Output -
    returns FSUR_MOUNT_HIDDEN (previously FSUR_RECOGNIZED) if we can handle the media else one of the FSUR_xyz error codes.
*************************************************************************************************** */
static int
DoProbe(char *deviceNamePtr)
{
	int result = FSUR_UNRECOGNIZED;
	int fd = 0;
	char * bufPtr;
	HFSMasterDirectoryBlock * mdbPtr;
	HFSPlusVolumeHeader * volHdrPtr;
	u_char volnameUTF8[NAME_MAX+1];

	bufPtr = (char *)malloc(HFS_BLOCK_SIZE);
	if ( ! bufPtr ) {
		result = FSUR_UNRECOGNIZED;
		goto Return;
	}

	mdbPtr = (HFSMasterDirectoryBlock *) bufPtr;
	volHdrPtr = (HFSPlusVolumeHeader *) bufPtr;

	fd = open( deviceNamePtr, O_RDONLY, 0 );
	if( fd <= 0 ) {
		result = FSUR_IO_FAIL;
		goto Return;
	}

	/*
	 * Read the HFS Master Directory Block from sector 2
	 */
	result = readAt(fd, bufPtr, (off_t)(2 * HFS_BLOCK_SIZE), HFS_BLOCK_SIZE);
	if (FSUR_IO_FAIL == result)
		goto Return;

	/* get classic HFS volume name (from MDB) */
	if (mdbPtr->drSigWord == kHFSSigWord &&
	    mdbPtr->drEmbedSigWord != kHFSPlusSigWord) {
	    	Boolean cfOK;
		CFStringRef cfstr;
		CFStringEncoding encoding;

		/* Some poorly mastered HFS CDs have an empty MDB name field! */
		if (mdbPtr->drVN[0] == '\0') {
			strcpy(&mdbPtr->drVN[1], gHFS_FS_NAME_NAME);
			mdbPtr->drVN[0] = strlen(gHFS_FS_NAME_NAME);
		}

		encoding = CFStringGetSystemEncoding();
		cfstr = CFStringCreateWithPascalString(kCFAllocatorDefault,
			    mdbPtr->drVN, encoding);
		cfOK = CFStringGetCString(cfstr, volnameUTF8, NAME_MAX,
			    kCFStringEncodingUTF8);
		CFRelease(cfstr);

		if (!cfOK && encoding != kCFStringEncodingMacRoman) {

		    	/* default to MacRoman on conversion errors */
			cfstr = CFStringCreateWithPascalString(kCFAllocatorDefault,
				    mdbPtr->drVN, kCFStringEncodingMacRoman);
			CFStringGetCString(cfstr, volnameUTF8, NAME_MAX,
			    kCFStringEncodingUTF8);
			CFRelease(cfstr);
		}
 
 	/* get HFS Plus volume name (from Catalog) */
	} else if ((volHdrPtr->signature == kHFSPlusSigWord)  ||
		   (mdbPtr->drSigWord == kHFSSigWord &&
		    mdbPtr->drEmbedSigWord == kHFSPlusSigWord)) {
		off_t startOffset;

		if (volHdrPtr->signature == kHFSPlusSigWord) {
			startOffset = 0;
		} else {/* embedded volume, first find offset */
			result = GetEmbeddedHFSPlusVol(mdbPtr, &startOffset);
			if ( result != FSUR_IO_SUCCESS )
				goto Return;
		}

		result = GetNameFromHFSPlusVolumeStartingAt(fd, startOffset,
			    volnameUTF8);
	} else {
		result = FSUR_UNRECOGNIZED;
	}

	if (FSUR_IO_SUCCESS == result) {
		CFStringRef slash;
		CFStringRef colon;
		CFMutableStringRef volumeName;
		CFIndex volumeNameLength;
		CFRange foundSubString;
		
		slash = CFStringCreateWithCString(kCFAllocatorDefault, "/", kCFStringEncodingUTF8);
		if (slash == NULL) {
			result = FSUR_IO_FAIL;
			goto Return;
		};
		
		colon = CFStringCreateWithCString(kCFAllocatorDefault, ":", kCFStringEncodingUTF8);
		if (colon == NULL) {
			result = FSUR_IO_FAIL;
			goto Return;
		};
		
		volumeName = CFStringCreateMutableCopy(
							kCFAllocatorDefault,
							0,							/* maxLength */
							CFStringCreateWithCString(kCFAllocatorDefault, volnameUTF8, kCFStringEncodingUTF8));
		if (volumeName == NULL) {
			result = FSUR_IO_FAIL;
			goto Return;
		};
		volumeNameLength = CFStringGetLength(volumeName);

		while (CFStringFindWithOptions(volumeName, slash, CFRangeMake(0, volumeNameLength-1), 0, &foundSubString)) {
			CFStringReplace(volumeName, foundSubString, colon);
		};

		CFStringGetCString(volumeName, volnameUTF8, NAME_MAX, kCFStringEncodingUTF8);
		write(1, volnameUTF8, strlen(volnameUTF8));

		/* backwards compatibility */
		DoFileSystemFile( FS_NAME_SUFFIX, gHFS_FS_NAME );
		DoFileSystemFile( FS_LABEL_SUFFIX, volnameUTF8 );
		result = FSUR_MOUNT_HIDDEN;
	}

Return:

	if ( bufPtr )
		free( bufPtr );

	if (fd > 0)
		close(fd);

	return result;

} /* DoProbe */



/* **************************************** DoGetUUIDKey *******************************************
Purpose -
    This routine will open the given block device and return the volume UUID in text form written to stdout.
Input -
    theDeviceNamePtr - pointer to the device name (full path, like /dev/disk0s2).
Output -
    returns FSUR_IO_SUCCESS or else one of the FSUR_xyz error codes.
*************************************************************************************************** */
static int
DoGetUUIDKey( const char * theDeviceNamePtr ) {
	int result;
	VolumeUUID targetVolumeUUID;
	VolumeUUIDString UUIDString;
	char uuidLine[VOLUMEUUIDLENGTH+2];
	
	if ((result = GetVolumeUUID(theDeviceNamePtr, &targetVolumeUUID, FALSE)) != FSUR_IO_SUCCESS) goto Err_Exit;
	
	ConvertVolumeUUIDToString( &targetVolumeUUID, UUIDString);
	strncpy(uuidLine, UUIDString, VOLUMEUUIDLENGTH+1);
	write(1, uuidLine, strlen(uuidLine));
	result = FSUR_IO_SUCCESS;
	
Err_Exit:
	return result;
}



/* *************************************** DoChangeUUIDKey ******************************************
Purpose -
    This routine will change the UUID on the specified block device.
Input -
    theDeviceNamePtr - pointer to the device name (full path, like /dev/disk0s2).
Output -
    returns FSUR_IO_SUCCESS or else one of the FSUR_xyz error codes.
*************************************************************************************************** */
static int
DoChangeUUIDKey( const char * theDeviceNamePtr ) {
	int result;
	VolumeUUID newVolumeUUID;
	
	GenerateVolumeUUID(&newVolumeUUID);
	result = SetVolumeUUID(theDeviceNamePtr, &newVolumeUUID);
	
	return result;
}



/* **************************************** DoAdopt *******************************************
Purpose -
    This routine will add the UUID of the specified block device to the list of local volumes.
Input -
    theDeviceNamePtr - pointer to the device name (full path, like /dev/disk0s2).
Output -
    returns FSUR_IO_SUCCESS or else one of the FSUR_xyz error codes.
*************************************************************************************************** */
static int
DoAdopt( const char * theDeviceNamePtr ) {
	int result, closeresult;
	VolumeUUID targetVolumeUUID;
	VolumeStatusDBHandle vsdbhandle = NULL;
	unsigned long targetVolumeStatus;
	
	if ((result = GetVolumeUUID(theDeviceNamePtr, &targetVolumeUUID, TRUE)) != FSUR_IO_SUCCESS) goto Err_Return;
	
	if ((result = OpenVolumeStatusDB(&vsdbhandle)) != 0) goto Err_Exit;
	if ((result = GetVolumeStatusDBEntry(vsdbhandle, &targetVolumeUUID, &targetVolumeStatus)) != 0) {
		targetVolumeStatus = 0;
	};
	targetVolumeStatus = (targetVolumeStatus & VOLUME_VALIDSTATUSBITS) | VOLUME_USEPERMISSIONS;
	if ((result = SetVolumeStatusDBEntry(vsdbhandle, &targetVolumeUUID, targetVolumeStatus)) != 0) goto Err_Exit;

	result = FSUR_IO_SUCCESS;
	
Err_Exit:
	if (vsdbhandle) {
		closeresult = CloseVolumeStatusDB(vsdbhandle);
		vsdbhandle = NULL;
		if (result == FSUR_IO_SUCCESS) result = closeresult;
	};
	
	if ((result != 0) && (result != FSUR_IO_SUCCESS)) result = FSUR_IO_FAIL;
	
Err_Return:
#if TRACE_HFS_UTIL
	if (result != FSUR_IO_SUCCESS) fprintf(stderr, "DoAdopt: returning %d...\n", result);
#endif
	return result;
}



/* **************************************** DoDisown *******************************************
Purpose -
    This routine will change the status of the specified block device to ignore its permissions.
Input -
    theDeviceNamePtr - pointer to the device name (full path, like /dev/disk0s2).
Output -
    returns FSUR_IO_SUCCESS or else one of the FSUR_xyz error codes.
*************************************************************************************************** */
static int
DoDisown( const char * theDeviceNamePtr ) {
	int result, closeresult;
	VolumeUUID targetVolumeUUID;
	VolumeStatusDBHandle vsdbhandle = NULL;
	unsigned long targetVolumeStatus;
	
	if ((result = GetVolumeUUID(theDeviceNamePtr, &targetVolumeUUID, TRUE)) != FSUR_IO_SUCCESS) goto Err_Return;
	
	if ((result = OpenVolumeStatusDB(&vsdbhandle)) != 0) goto Err_Exit;
	if ((result = GetVolumeStatusDBEntry(vsdbhandle, &targetVolumeUUID, &targetVolumeStatus)) != 0) {
		targetVolumeStatus = 0;
	};
	targetVolumeStatus = (targetVolumeStatus & VOLUME_VALIDSTATUSBITS) & ~VOLUME_USEPERMISSIONS;
	if ((result = SetVolumeStatusDBEntry(vsdbhandle, &targetVolumeUUID, targetVolumeStatus)) != 0) goto Err_Exit;

	result = FSUR_IO_SUCCESS;
	
Err_Exit:
	if (vsdbhandle) {
		closeresult = CloseVolumeStatusDB(vsdbhandle);
		vsdbhandle = NULL;
		if (result == FSUR_IO_SUCCESS) result = closeresult;
	};
	
	if ((result != 0) && (result != FSUR_IO_SUCCESS)) {
#if TRACE_HFS_UTIL
		if (result != 0) fprintf(stderr, "DoDisown: result = %d; changing to %d...\n", result, FSUR_IO_FAIL);
#endif
		result = FSUR_IO_FAIL;
	};
	
Err_Return:
#if TRACE_HFS_UTIL
	if (result != FSUR_IO_SUCCESS) fprintf(stderr, "DoDisown: returning %d...\n", result);
#endif
	return result;
}


static int
get_multiplier(char c)
{
	if (tolower(c) == 'k') {
		return 1024;
	} else if (tolower(c) == 'm') {
		return 1024 * 1024;
	} else if (tolower(c) == 'g') {
		return 1024 * 1024 * 1024;
	} 

	return 1;
}

/* **************************************** ParseArgs ********************************************
Purpose -
    This routine will make sure the arguments passed in to us are cool.
    Here is how this utility is used:

usage: hfs.util actionArg deviceArg [mountPointArg] [flagsArg]
actionArg:
        -p (Probe for mounting)
       	-P (Probe for initializing - not supported)
       	-m (Mount)
        -r (Repair - not supported)
        -u (Unmount)
        -M (Force Mount)
        -i (Initialize - not supported)

deviceArg:
        disk0s2 (for example)

mountPointArg:
        /foo/bar/ (required for Mount and Force Mount actions)

flagsArg:
        (these are ignored for CDROMs)
        either "readonly" OR "writable"
        either "removable" OR "fixed"
        either "nosuid" or "suid"

examples:
	hfs.util -p disk0s2 removable writable
	hfs.util -p disk0s2 removable readonly
	hfs.util -m disk0s2 /my/hfs

Input -
    argc - the number of arguments in argv.
    argv - array of arguments.
Output -
    returns FSUR_INVAL if we find a bad argument else 0.
*************************************************************************************************** */
static int
ParseArgs(int argc, const char *argv[], const char ** actionPtr,
	const char ** mountPointPtr, boolean_t * isEjectablePtr,
          boolean_t * isLockedPtr, boolean_t * isSetuidPtr, boolean_t * isDevPtr)
{
    int			result = FSUR_INVAL;
    int			deviceLength, doLengthCheck = 1;
    int			index;
    int 		mounting = 0;

    /* Must have at least 3 arguments and the action argument must start with a '-' */
    if ( (argc < 3) || (argv[1][0] != '-') ) {
        DoDisplayUsage( argv );
        goto Return;
    }

    /* we only support actions Probe, Mount, Force Mount, and Unmount */

    * actionPtr = & argv[1][1];

    switch ( argv[1][1] ) {
        case FSUC_PROBE:
        /* action Probe and requires 5 arguments (need the flags) */
	     if ( argc < 5 ) {
             	DoDisplayUsage( argv );
             	goto Return;
	     } else {
            	index = 3;
	     }
            break;
            
        case FSUC_UNMOUNT:
        	/* Note: the device argument in argv[2] is checked further down but ignored. */
            * mountPointPtr = argv[3];
            index = 0; /* No isEjectable/isLocked flags for unmount. */
            break;
            
        case FSUC_MOUNT:
        case FSUC_MOUNT_FORCE:
            /* action Mount and ForceMount require 7 arguments (need the mountpoint and the flags) */
            if ( argc < 7 ) {
                DoDisplayUsage( argv );
                goto Return;
            } else {
                * mountPointPtr = argv[3];
                index = 4;
                mounting = 1;
            }
            break;
        
        case FSUC_GETUUID:
        	index = 0;
			break;
		
		case FSUC_SETUUID:
			index = 0;
			break;
			
		case FSUC_ADOPT:
			index = 0;
			break;
		
		case FSUC_DISOWN:
			index = 0;
			break;
		
		// XXXdbg
		case FSUC_MKJNL:
			index = 0;
			doLengthCheck = 0;
			if (isdigit(argv[2][0])) {
				char *ptr;
				gJournalSize = strtoul(argv[2], &ptr, 0);
				if (ptr) {
					gJournalSize *= get_multiplier(*ptr);
				}
				return 0;
			}
			break;

		case FSUC_UNJNL:
			index = 0;
			doLengthCheck = 0;
			break;
		// XXXdbg

        default:
            DoDisplayUsage( argv );
            goto Return;
            break;
    }

    /* Make sure device (argv[2]) is something reasonable */
    deviceLength = strlen( argv[2] );
    if ( doLengthCheck && (deviceLength < 3 || deviceLength > NAME_MAX) ) {
        DoDisplayUsage( argv );
        goto Return;
    }

    if ( index ) {
        /* Flags: removable/fixed. */
        if ( 0 == strcmp(argv[index],"removable") ) {
            * isEjectablePtr = 1;
        } else if ( 0 == strcmp(argv[index],"fixed") ) {
            * isEjectablePtr = 0;
        } else {
            printf("hfs.util: ERROR: unrecognized flag (removable/fixed) argv[%d]='%s'\n",index,argv[index]);
        }

        /* Flags: readonly/writable. */
        if ( 0 == strcmp(argv[index+1],"readonly") ) {
            * isLockedPtr = 1;
        } else if ( 0 == strcmp(argv[index+1],"writable") ) {
            * isLockedPtr = 0;
        } else {
            printf("hfs.util: ERROR: unrecognized flag (readonly/writable) argv[%d]='%s'\n",index,argv[index+1]);
        }

        if (mounting) {
                    /* Flags: suid/nosuid. */
                    if ( 0 == strcmp(argv[index+2],"suid") ) {
                        * isSetuidPtr = 1;
                    } else if ( 0 == strcmp(argv[index+2],"nosuid") ) {
                        * isSetuidPtr = 0;
                    } else {
                        printf("hfs.util: ERROR: unrecognized flag (suid/nosuid) argv[%d]='%s'\n",index,argv[index+2]);
                    }

                    /* Flags: dev/nodev. */
                    if ( 0 == strcmp(argv[index+3],"dev") ) {
                        * isDevPtr = 1;
                    } else if ( 0 == strcmp(argv[index+3],"nodev") ) {
                        * isDevPtr = 0;
                    } else {
                        printf("hfs.util: ERROR: unrecognized flag (dev/nodev) argv[%d]='%s'\n",index,argv[index+3]);
                    }
        }


    }

    result = 0;

Return:
        return result;

} /* ParseArgs */


/* *************************************** DoDisplayUsage ********************************************
Purpose -
    This routine will do a printf of the correct usage for this utility.
Input -
    argv - array of arguments.
Output -
    NA.
*************************************************************************************************** */
static void
DoDisplayUsage(const char *argv[])
{
    printf("usage: %s action_arg device_arg [mount_point_arg] [Flags] \n", argv[0]);
    printf("action_arg:\n");
    printf("       -%c (Probe for mounting)\n", FSUC_PROBE);
    printf("       -%c (Mount)\n", FSUC_MOUNT);
    printf("       -%c (Unmount)\n", FSUC_UNMOUNT);
    printf("       -%c (Force Mount)\n", FSUC_MOUNT_FORCE);
#ifdef HFS_UUID_SUPPORT
    printf("       -%c (Get UUID Key)\n", FSUC_GETUUID);
    printf("       -%c (Set UUID Key)\n", FSUC_SETUUID);
#endif HFS_UUID_SUPPORT
    printf("       -%c (Adopt permissions)\n", FSUC_ADOPT);
	printf("       -%c (Make a volume journaled)\n", FSUC_MKJNL);
	printf("       -%c (Turn off journaling on a volume)\n", FSUC_UNJNL);
    printf("device_arg:\n");
    printf("       device we are acting upon (for example, 'disk0s2')\n");
    printf("       if '-%c' or '-%c' is specified, this should be the\n", FSUC_MKJNL, FSUC_UNJNL);
	printf("       name of the volume we to act on (for example, '/Volumes/foo' or '/')\n");
    printf("mount_point_arg:\n");
    printf("       required for Mount and Force Mount \n");
    printf("Flags:\n");
    printf("       required for Mount, Force Mount and Probe\n");
    printf("       indicates removable or fixed (for example 'fixed')\n");
    printf("       indicates readonly or writable (for example 'readonly')\n");
    printf("Examples:\n");
    printf("       %s -p disk0s2 fixed writable\n", argv[0]);
    printf("       %s -m disk0s2 /my/hfs removable readonly\n", argv[0]);

    return;

} /* DoDisplayUsage */


/* ************************************** DoFileSystemFile *******************************************
Purpose -
    This routine will create a file system info file that is used by WorkSpace.  After creating the
    file it will write whatever theContentsPtr points to the new file.
    We end up with a file something like:
    /System/Library/Filesystems/hfs.fs/hfs.name
    when our file system name is "hfs" and theFileNameSuffixPtr points to ".name"
Input -
    theFileNameSuffixPtr - pointer to a suffix we add to the file name we're creating.
    theContentsPtr - pointer to C string to write into the file.
Output -
    NA.
*************************************************************************************************** */
static void
DoFileSystemFile(char *fileNameSuffixPtr, char *contentsPtr)
{
    int fd;
    char fileName[MAXPATHLEN];

    sprintf(fileName, "%s/%s%s/%s", FS_DIR_LOCATION, gHFS_FS_NAME,
    	FS_DIR_SUFFIX, gHFS_FS_NAME );
    strcat(fileName, fileNameSuffixPtr );
    unlink(fileName);		/* delete existing string */

    if ( strlen( fileNameSuffixPtr ) ) {
        int oldMask = umask(0);

        fd = open( & fileName[0], O_CREAT | O_TRUNC | O_WRONLY, 0644 );
        umask( oldMask );
        if ( fd > 0 ) {
            write( fd, contentsPtr, strlen( contentsPtr ) );
            close( fd );
        }
    }

    return;

} /* DoFileSystemFile */

/*
 --	GetVolumeUUID
 --
 --	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 */

static int
GetVolumeUUID(const char *deviceNamePtr, VolumeUUID *volumeUUIDPtr, boolean_t generate) {
	int fd = 0;
	char * bufPtr;
	HFSMasterDirectoryBlock * mdbPtr;
	HFSPlusVolumeHeader * volHdrPtr;
	VolumeUUID *finderInfoUUIDPtr;
	int result;

	bufPtr = (char *)malloc(HFS_BLOCK_SIZE);
	if ( ! bufPtr ) {
		result = FSUR_UNRECOGNIZED;
		goto Err_Exit;
	}

	mdbPtr = (HFSMasterDirectoryBlock *) bufPtr;
	volHdrPtr = (HFSPlusVolumeHeader *) bufPtr;

	fd = open( deviceNamePtr, O_RDWR, 0 );
	if( fd <= 0 ) {
		fd = open( deviceNamePtr, O_RDONLY, 0);
		if (fd <= 0) {
#if TRACE_HFS_UTIL
			fprintf(stderr, "hfs.util: GetVolumeUUID: device open failed (errno = %d).\n", errno);
#endif
			result = FSUR_IO_FAIL;
			goto Err_Exit;
		};
	};

	/*
	 * Read the HFS Master Directory Block from sector 2
	 */
	result = readAt(fd, volHdrPtr, (off_t)(2 * HFS_BLOCK_SIZE), HFS_BLOCK_SIZE);
	if (result != FSUR_IO_SUCCESS) {
		goto Err_Exit;
	};

	if (mdbPtr->drSigWord == kHFSSigWord &&
	    mdbPtr->drEmbedSigWord != kHFSPlusSigWord) {
	    finderInfoUUIDPtr = (VolumeUUID *)(&mdbPtr->drFndrInfo[6]);
	    if (generate && ((finderInfoUUIDPtr->v.high == 0) || (finderInfoUUIDPtr->v.low == 0))) {
	    	GenerateVolumeUUID(volumeUUIDPtr);
	    	bcopy(volumeUUIDPtr, finderInfoUUIDPtr, sizeof(*finderInfoUUIDPtr));
			result = writeAt(fd, volHdrPtr, (off_t)(2 * HFS_BLOCK_SIZE), HFS_BLOCK_SIZE);
			if (result != FSUR_IO_SUCCESS) goto Err_Exit;
		};
		bcopy(finderInfoUUIDPtr, volumeUUIDPtr, sizeof(*volumeUUIDPtr));
		result = FSUR_IO_SUCCESS;
	} else if ((volHdrPtr->signature == kHFSPlusSigWord)  ||
		   ((mdbPtr->drSigWord == kHFSSigWord) &&
		    (mdbPtr->drEmbedSigWord == kHFSPlusSigWord))) {
		off_t startOffset;

		if (volHdrPtr->signature == kHFSPlusSigWord) {
			startOffset = 0;
		} else {/* embedded volume, first find offset */
			result = GetEmbeddedHFSPlusVol(mdbPtr, &startOffset);
			if ( result != FSUR_IO_SUCCESS ) {
				goto Err_Exit;
			};
		}
	
	    result = readAt( fd, volHdrPtr, startOffset + (off_t)(2*HFS_BLOCK_SIZE), HFS_BLOCK_SIZE );
	    if (result != FSUR_IO_SUCCESS) {
	        goto Err_Exit; // return FSUR_IO_FAIL
	    }
	
	    /* Verify that it is an HFS+ volume. */
	
	    if (volHdrPtr->signature != kHFSPlusSigWord) {
	        result = FSUR_IO_FAIL;
	        goto Err_Exit;
	    }
	
	    finderInfoUUIDPtr = (VolumeUUID *)&volHdrPtr->finderInfo[24];
	    if (generate && ((finderInfoUUIDPtr->v.high == 0) || (finderInfoUUIDPtr->v.low == 0))) {
	    	GenerateVolumeUUID(volumeUUIDPtr);
	    	bcopy(volumeUUIDPtr, finderInfoUUIDPtr, sizeof(*finderInfoUUIDPtr));
			result = writeAt( fd, volHdrPtr, startOffset + (off_t)(2*HFS_BLOCK_SIZE), HFS_BLOCK_SIZE );
			if (result != FSUR_IO_SUCCESS) {
				goto Err_Exit;
			};
		};
		bcopy(finderInfoUUIDPtr, volumeUUIDPtr, sizeof(*volumeUUIDPtr));
		result = FSUR_IO_SUCCESS;
	} else {
		result = FSUR_UNRECOGNIZED;
	};

Err_Exit:
	if (fd > 0) close(fd);
	if (bufPtr) free(bufPtr);
	
#if TRACE_HFS_UTIL
	if (result != FSUR_IO_SUCCESS) fprintf(stderr, "hfs.util: GetVolumeUUID: result = %d...\n", result);
#endif
	return (result == FSUR_IO_SUCCESS) ? FSUR_IO_SUCCESS : FSUR_IO_FAIL;
};



/*
 --	SetVolumeUUID
 --
 --	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 */

static int
SetVolumeUUID(const char *deviceNamePtr, VolumeUUID *volumeUUIDPtr) {
	int fd = 0;
	char * bufPtr;
	HFSMasterDirectoryBlock * mdbPtr;
	HFSPlusVolumeHeader * volHdrPtr;
	VolumeUUID *finderInfoUUIDPtr;
	int result;

	bufPtr = (char *)malloc(HFS_BLOCK_SIZE);
	if ( ! bufPtr ) {
		result = FSUR_UNRECOGNIZED;
		goto Err_Exit;
	}

	mdbPtr = (HFSMasterDirectoryBlock *) bufPtr;
	volHdrPtr = (HFSPlusVolumeHeader *) bufPtr;

	fd = open( deviceNamePtr, O_RDWR, 0 );
	if( fd <= 0 ) {
#if TRACE_HFS_UTIL
		fprintf(stderr, "hfs.util: SetVolumeUUID: device open failed (errno = %d).\n", errno);
#endif
		result = FSUR_IO_FAIL;
		goto Err_Exit;
	};

	/*
	 * Read the HFS Master Directory Block from sector 2
	 */
	result = readAt(fd, volHdrPtr, (off_t)(2 * HFS_BLOCK_SIZE), HFS_BLOCK_SIZE);
	if (result != FSUR_IO_SUCCESS) {
		goto Err_Exit;
	};

	if (mdbPtr->drSigWord == kHFSSigWord &&
	    mdbPtr->drEmbedSigWord != kHFSPlusSigWord) {
	    finderInfoUUIDPtr = (VolumeUUID *)(&mdbPtr->drFndrInfo[6]);
	    bcopy(volumeUUIDPtr, finderInfoUUIDPtr, sizeof(*finderInfoUUIDPtr));
		result = writeAt(fd, volHdrPtr, (off_t)(2 * HFS_BLOCK_SIZE), HFS_BLOCK_SIZE);
	} else if ((volHdrPtr->signature == kHFSPlusSigWord)  ||
		   ((mdbPtr->drSigWord == kHFSSigWord) &&
		    (mdbPtr->drEmbedSigWord == kHFSPlusSigWord))) {
		off_t startOffset;

		if (volHdrPtr->signature == kHFSPlusSigWord) {
			startOffset = 0;
		} else {/* embedded volume, first find offset */
			result = GetEmbeddedHFSPlusVol(mdbPtr, &startOffset);
			if ( result != FSUR_IO_SUCCESS ) {
				goto Err_Exit;
			};
		}
	
	    result = readAt( fd, volHdrPtr, startOffset + (off_t)(2*HFS_BLOCK_SIZE), HFS_BLOCK_SIZE );
	    if (result != FSUR_IO_SUCCESS) {
	        goto Err_Exit; // return FSUR_IO_FAIL
	    }
	
	    /* Verify that it is an HFS+ volume. */
	
	    if (volHdrPtr->signature != kHFSPlusSigWord) {
	        result = FSUR_IO_FAIL;
	        goto Err_Exit;
	    }
	
	    finderInfoUUIDPtr = (VolumeUUID *)&volHdrPtr->finderInfo[24];
	    bcopy(volumeUUIDPtr, finderInfoUUIDPtr, sizeof(*finderInfoUUIDPtr));
		result = writeAt( fd, volHdrPtr, startOffset + (off_t)(2*HFS_BLOCK_SIZE), HFS_BLOCK_SIZE );
		if (result != FSUR_IO_SUCCESS) {
			goto Err_Exit;
		};
		result = FSUR_IO_SUCCESS;
	} else {
		result = FSUR_UNRECOGNIZED;
	};

Err_Exit:
	if (fd > 0) close(fd);
	if (bufPtr) free(bufPtr);
	
#if TRACE_HFS_UTIL
	if (result != FSUR_IO_SUCCESS) fprintf(stderr, "hfs.util: SetVolumeUUID: result = %d...\n", result);
#endif
	return (result == FSUR_IO_SUCCESS) ? FSUR_IO_SUCCESS : FSUR_IO_FAIL;
};



/*
 --	GetEmbeddedHFSPlusVol
 --
 --	In: hfsMasterDirectoryBlockPtr
 --	Out: startOffsetPtr - the disk offset at which the HFS+ volume starts
 				(that is, 2 blocks before the volume header)
 --
 */

static int
GetEmbeddedHFSPlusVol (HFSMasterDirectoryBlock * hfsMasterDirectoryBlockPtr, off_t * startOffsetPtr)
{
    int		result = FSUR_IO_SUCCESS;
    u_int32_t	allocationBlockSize, firstAllocationBlock, startBlock, blockCount;

    if (hfsMasterDirectoryBlockPtr->drSigWord != kHFSSigWord) {
        result = FSUR_UNRECOGNIZED;
        goto Return;
    }

    allocationBlockSize = hfsMasterDirectoryBlockPtr->drAlBlkSiz;
    firstAllocationBlock = hfsMasterDirectoryBlockPtr->drAlBlSt;

    if (hfsMasterDirectoryBlockPtr->drEmbedSigWord != kHFSPlusSigWord) {
        result = FSUR_UNRECOGNIZED;
        goto Return;
    }

    startBlock = hfsMasterDirectoryBlockPtr->drEmbedExtent.startBlock;
    blockCount = hfsMasterDirectoryBlockPtr->drEmbedExtent.blockCount;

    if ( startOffsetPtr )
        *startOffsetPtr = ((u_int64_t)startBlock * (u_int64_t)allocationBlockSize) +
        	((u_int64_t)firstAllocationBlock * (u_int64_t)HFS_BLOCK_SIZE);

Return:
        return result;

}



/*
 --	GetNameFromHFSPlusVolumeStartingAt
 --
 --	Caller's responsibility to allocate and release memory for the converted string.
 --
 --	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 */

static int
GetNameFromHFSPlusVolumeStartingAt(int fd, off_t hfsPlusVolumeOffset, char * name_o)
{
    int					result = FSUR_IO_SUCCESS;
    u_int32_t				blockSize;
    char			*	bufPtr = NULL;
    HFSPlusVolumeHeader		*	volHdrPtr;
    BTNodeDescriptor		*	bTreeNodeDescriptorPtr;
    u_int32_t				catalogNodeSize;
	u_int32_t leafNode;
	u_int32_t catalogExtCount;
	HFSPlusExtentDescriptor *catalogExtents = NULL;

    volHdrPtr = (HFSPlusVolumeHeader *)malloc(HFS_BLOCK_SIZE);
    if ( ! volHdrPtr ) {
        result = FSUR_IO_FAIL;
        goto Return;
    }

    /*
     * Read the Volume Header
     * (This is a little redundant for a pure, unwrapped HFS+ volume)
     */
    result = readAt( fd, volHdrPtr, hfsPlusVolumeOffset + (off_t)(2*HFS_BLOCK_SIZE), HFS_BLOCK_SIZE );
    if (result == FSUR_IO_FAIL) {
#if TRACE_HFS_UTIL
        fprintf(stderr, "hfs.util: GetNameFromHFSPlusVolumeStartingAt: readAt failed\n");
#endif
        goto Return; // return FSUR_IO_FAIL
    }

    /* Verify that it is an HFS+ volume. */

    if (volHdrPtr->signature != kHFSPlusSigWord) {
        result = FSUR_IO_FAIL;
#if TRACE_HFS_UTIL
        fprintf(stderr, "hfs.util: GetNameFromHFSPlusVolumeStartingAt: volHdrPtr->signature != kHFSPlusSigWord\n");
#endif
        goto Return;
    }

    blockSize = volHdrPtr->blockSize;
    catalogExtents = (HFSPlusExtentDescriptor *) malloc(sizeof(HFSPlusExtentRecord));
    if ( ! catalogExtents ) {
        result = FSUR_IO_FAIL;
        goto Return;
    }
	bcopy(volHdrPtr->catalogFile.extents, catalogExtents, sizeof(HFSPlusExtentRecord));
	catalogExtCount = kHFSPlusExtentDensity;

	/* if there are overflow catalog extents, then go get them */
	if (catalogExtents[7].blockCount != 0) {
		result = GetCatalogOverflowExtents(fd, hfsPlusVolumeOffset, volHdrPtr, &catalogExtents, &catalogExtCount);
		if (result != FSUR_IO_SUCCESS)
			goto Return;
	}

	/* Read the header node of the catalog B-Tree */

	result = GetBTreeNodeInfo(fd, hfsPlusVolumeOffset, blockSize,
							catalogExtCount, catalogExtents,
							&catalogNodeSize, &leafNode);
	if (result != FSUR_IO_SUCCESS)
        goto Return;

	/* Read the first leaf node of the catalog b-tree */

    bufPtr = (char *)malloc(catalogNodeSize);
    if ( ! bufPtr ) {
        result = FSUR_IO_FAIL;
        goto Return;
    }

    bTreeNodeDescriptorPtr = (BTNodeDescriptor *)bufPtr;

	result = ReadFile(fd, bufPtr, (off_t) leafNode * (off_t) catalogNodeSize, catalogNodeSize,
						hfsPlusVolumeOffset, blockSize,
						catalogExtCount, catalogExtents);
    if (result == FSUR_IO_FAIL) {
#if TRACE_HFS_UTIL
        fprintf(stderr, "hfs.util: ERROR: reading first leaf failed\n");
#endif
        goto Return; // return FSUR_IO_FAIL
    }

    {
        u_int16_t			*	v;
        char			*	p;
        HFSPlusCatalogKey	*	k;
	CFStringRef cfstr;

        if ( bTreeNodeDescriptorPtr->numRecords < 1) {
            result = FSUR_IO_FAIL;
#if TRACE_HFS_UTIL
			fprintf(stderr, "hfs.util: ERROR: bTreeNodeDescriptorPtr->numRecords < 1\n");
#endif
            goto Return;
        }

	// Get the offset (in bytes) of the first record from the list of offsets at the end of the node.

        p = bufPtr + catalogNodeSize - sizeof(u_int16_t); // pointer arithmetic in bytes
        v = (u_int16_t *)p;

	// Get a pointer to the first record.

        p = bufPtr + *v; // pointer arithmetic in bytes
        k = (HFSPlusCatalogKey *)p;

	// There should be only one record whose parent is the root parent.  It should be the first record.

        if (k->parentID != kHFSRootParentID) {
            result = FSUR_IO_FAIL;
#if TRACE_HFS_UTIL
            fprintf(stderr, "hfs.util: ERROR: k->parentID != kHFSRootParentID\n");
#endif
            goto Return;
        }

	/* Extract the name of the root directory */

	cfstr = CFStringCreateWithCharacters(kCFAllocatorDefault, k->nodeName.unicode, k->nodeName.length);
	(void) CFStringGetCString(cfstr, name_o, NAME_MAX, kCFStringEncodingUTF8);
	CFRelease(cfstr);
    }

    result = FSUR_IO_SUCCESS;

Return:
	if (volHdrPtr)
		free((char*) volHdrPtr);

	if (catalogExtents)
		free((char*) catalogExtents);
		
	if (bufPtr)
		free((char*)bufPtr);

    return result;

} /* GetNameFromHFSPlusVolumeStartingAt */


#pragma options align=mac68k
typedef struct {
	BTNodeDescriptor	node;
	BTHeaderRec		header;
} HeaderRec, *HeaderPtr;
#pragma options align=reset

/*
 --	
 --
 --	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 --
 */
static int
GetBTreeNodeInfo(int fd, off_t hfsPlusVolumeOffset, u_int32_t blockSize,
				u_int32_t extentCount, const HFSPlusExtentDescriptor *extentList,
				u_int32_t *nodeSize, u_int32_t *firstLeafNode)
{
	int result;
	HeaderRec * bTreeHeaderPtr = NULL;

	bTreeHeaderPtr = (HeaderRec *) malloc(HFS_BLOCK_SIZE);
	if (bTreeHeaderPtr == NULL)
		return (FSUR_IO_FAIL);
    
	/* Read the b-tree header node */

	result = ReadFile(fd, bTreeHeaderPtr, 0, HFS_BLOCK_SIZE,
					hfsPlusVolumeOffset, blockSize,
					extentCount, extentList);
	if ( result == FSUR_IO_FAIL ) {
#if TRACE_HFS_UTIL
		fprintf(stderr, "hfs.util: ERROR: reading header node failed\n");
#endif
		goto free;
	}

	if ( bTreeHeaderPtr->node.kind != kBTHeaderNode ) {
		result = FSUR_IO_FAIL;
#if TRACE_HFS_UTIL
		fprintf(stderr, "hfs.util: ERROR: bTreeHeaderPtr->node.kind != kBTHeaderNode\n");
#endif
		goto free;
	}

	*nodeSize = bTreeHeaderPtr->header.nodeSize;

	if (bTreeHeaderPtr->header.leafRecords == 0)
		*firstLeafNode = 0;
	else
		*firstLeafNode = bTreeHeaderPtr->header.firstLeafNode;

free:;
	free((char*) bTreeHeaderPtr);

	return result;

} /* GetBTreeNodeInfo */


/*
 --
 --
 --	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 --
 */
static int
GetCatalogOverflowExtents(int fd, off_t hfsPlusVolumeOffset,
		HFSPlusVolumeHeader *volHdrPtr,
		HFSPlusExtentDescriptor **catalogExtents,
		u_int32_t *catalogExtCount)
{
	off_t offset;
	u_int32_t nodeSize;
	u_int32_t leafNode;
    BTNodeDescriptor * bTreeNodeDescriptorPtr;
	HFSPlusExtentDescriptor * extents;
	size_t listsize;
    char *	bufPtr = NULL;
	int i;
	int result;

	listsize = *catalogExtCount * sizeof(HFSPlusExtentDescriptor);
	extents = *catalogExtents;
	offset = (off_t)volHdrPtr->extentsFile.extents[0].startBlock *
		    (off_t)volHdrPtr->blockSize;

	/* Read the header node of the extents B-Tree */

	result = GetBTreeNodeInfo(fd, hfsPlusVolumeOffset, volHdrPtr->blockSize,
			kHFSPlusExtentDensity, volHdrPtr->extentsFile.extents,
		    &nodeSize, &leafNode);
	if (result != FSUR_IO_SUCCESS || leafNode == 0)
		goto Return;

	/* Calculate the logical position of the first leaf node */

	offset = (off_t) leafNode * (off_t) nodeSize;

	/* Read the first leaf node of the extents b-tree */

    bufPtr = (char *)malloc(nodeSize);
	if (! bufPtr) {
		result = FSUR_IO_FAIL;
		goto Return;
	}

	bTreeNodeDescriptorPtr = (BTNodeDescriptor *)bufPtr;

again:
	result = ReadFile(fd, bufPtr, offset, nodeSize,
					hfsPlusVolumeOffset, volHdrPtr->blockSize,
					kHFSPlusExtentDensity, volHdrPtr->extentsFile.extents);
	if ( result == FSUR_IO_FAIL ) {
#if TRACE_HFS_UTIL
		fprintf(stderr, "hfs.util: ERROR: reading first leaf failed\n");
#endif
		goto Return;
	}

	if (bTreeNodeDescriptorPtr->kind != kBTLeafNode) {
		result = FSUR_IO_FAIL;
		goto Return;
	}

	for (i = 1; i <= bTreeNodeDescriptorPtr->numRecords; ++i) {
		u_int16_t * v;
		char * p;
		HFSPlusExtentKey * k;

		/*
		 * Get the offset (in bytes) of the record from the
		 * list of offsets at the end of the node
		 */
		p = bufPtr + nodeSize - (sizeof(u_int16_t) * i);
		v = (u_int16_t *)p;

		/* Get a pointer to the record */

		p = bufPtr + *v; /* pointer arithmetic in bytes */
		k = (HFSPlusExtentKey *)p;

		if (k->fileID != kHFSCatalogFileID)
			goto Return;

		/* grow list and copy additional extents */
		listsize += sizeof(HFSPlusExtentRecord);
		extents = (HFSPlusExtentDescriptor *) realloc(extents, listsize);
		bcopy(p + k->keyLength + sizeof(u_int16_t),
			&extents[*catalogExtCount], sizeof(HFSPlusExtentRecord));

		*catalogExtCount += kHFSPlusExtentDensity;
		*catalogExtents = extents;
	}
	
	if ((leafNode = bTreeNodeDescriptorPtr->fLink) != 0) {
	
		offset = (off_t) leafNode * (off_t) nodeSize;
		
		goto again;
	}

Return:;
	if (bufPtr)
		free(bufPtr);

	return (result);
}



/*
 *	LogicalToPhysical - Map a logical file position and size to volume-relative physical
 *	position and number of contiguous bytes at that position.
 *
 *	Inputs:
 *		logicalOffset	Logical offset in bytes from start of file
 *		length			Maximum number of bytes to map
 *		blockSize		Number of bytes per allocation block
 *		extentCount		Number of extents in file
 *		extentList		The file's extents
 *
 *	Outputs:
 *		physicalOffset	Physical offset in bytes from start of volume
 *		availableBytes	Number of bytes physically contiguous (up to length)
 *
 *	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 */
static int	LogicalToPhysical(off_t offset, ssize_t length, u_int32_t blockSize,
							u_int32_t extentCount, const HFSPlusExtentDescriptor *extentList,
							off_t *physicalOffset, ssize_t *availableBytes)
{
	off_t		temp;
	u_int32_t	logicalBlock;
	u_int32_t	extent;
	u_int32_t	blockCount = 0;
	
	/* Determine allocation block containing logicalOffset */
	logicalBlock = offset / blockSize;	/* This can't overflow for valid volumes */
	offset %= blockSize;	/* Offset from start of allocation block */
	
	/* Find the extent containing logicalBlock */
	for (extent = 0; extent < extentCount; ++extent)
	{
		blockCount = extentList[extent].blockCount;
		
		if (blockCount == 0)
			return FSUR_IO_FAIL;	/* Tried to map past physical end of file */
		
		if (logicalBlock < blockCount)
			break;				/* Found it! */
		
		logicalBlock -= blockCount;
	}

	if (extent >= extentCount)
		return FSUR_IO_FAIL;		/* Tried to map past physical end of file */

	/*
	 *	When we get here, extentList[extent] is the extent containing logicalOffset.
	 *	The desired allocation block is logicalBlock blocks into the extent.
	 */
	
	/* Compute the physical starting position */
	temp = extentList[extent].startBlock + logicalBlock;	/* First physical block */
	temp *= blockSize;	/* Byte offset of first physical block */
	*physicalOffset = temp + offset;

	/* Compute the available contiguous bytes. */
	temp = blockCount - logicalBlock;	/* Number of blocks available in extent */
	temp *= blockSize;
	temp -= offset;						/* Number of bytes available */
	
	if (temp < length)
		*availableBytes = temp;
	else
		*availableBytes = length;
	
	return FSUR_IO_SUCCESS;
}



/*
 *	ReadFile - Read bytes from a file.  Handles cases where the starting and/or
 *	ending position are not allocation or device block aligned.
 *
 *	Inputs:
 *		fd			Descriptor for reading the volume
 *		buffer		The bytes are read into here
 *		offset		Offset in file to start reading
 *		length		Number of bytes to read
 *		volOffset	Byte offset from start of device to start of volume
 *		blockSize	Number of bytes per allocation block
 *		extentCount	Number of extents in file
 *		extentList	The file's exents
 *
 *	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 */
static int	ReadFile(int fd, void *buffer, off_t offset, ssize_t length,
					off_t volOffset, u_int32_t blockSize,
					u_int32_t extentCount, const HFSPlusExtentDescriptor *extentList)
{
	int		result = FSUR_IO_SUCCESS;
	off_t	physOffset;
	ssize_t	physLength;
	
	while (length > 0)
	{
		result = LogicalToPhysical(offset, length, blockSize, extentCount, extentList,
									&physOffset, &physLength);
		if (result != FSUR_IO_SUCCESS)
			break;
		
		result = readAt(fd, buffer, volOffset+physOffset, physLength);
		if (result != FSUR_IO_SUCCESS)
			break;
		
		length -= physLength;
		offset += physLength;
		buffer = (char *) buffer + physLength;
	}
	
	return result;
}

/*
 --	readAt = lseek() + read()
 --
 --	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 --
 */

static ssize_t
readAt( int fd, void * bufPtr, off_t offset, ssize_t length )
{
    int			blocksize;
    off_t		lseekResult;
    ssize_t		readResult;
    void *		rawData = NULL;
    off_t		rawOffset;
    ssize_t		rawLength;
    ssize_t		dataOffset = 0;
    int			result = FSUR_IO_SUCCESS;

    if (ioctl(fd, DKIOCBLKSIZE, &blocksize) < 0) {
#if TRACE_HFS_UTIL
    	fprintf(stderr, "hfs.util: readAt: couldn't determine block size of device.\n");
#endif
		result = FSUR_IO_FAIL;
		goto Return;
    }
    /* put offset and length in terms of device blocksize */
    rawOffset = offset / blocksize * blocksize;
    dataOffset = offset - rawOffset;
    rawLength = ((length + dataOffset + blocksize - 1) / blocksize) * blocksize;
    rawData = malloc(rawLength);
    if (rawData == NULL) {
		result = FSUR_IO_FAIL;
		goto Return;
    }

    lseekResult = lseek( fd, rawOffset, SEEK_SET );
    if ( lseekResult != rawOffset ) {
        result = FSUR_IO_FAIL;
        goto Return;
    }

    readResult = read(fd, rawData, rawLength);
    if ( readResult != rawLength ) {
#if TRACE_HFS_UTIL
    		fprintf(stderr, "hfs.util: readAt: attempt to read data from device failed (errno = %d)?\n", errno);
#endif
        result = FSUR_IO_FAIL;
        goto Return;
    }
    bcopy(rawData + dataOffset, bufPtr, length);

Return:
    if (rawData) {
        free(rawData);
    }
    return result;

} /* readAt */

/*
 --	writeAt = lseek() + write()
 --
 --	Returns: FSUR_IO_SUCCESS, FSUR_IO_FAIL
 --
 */

static ssize_t
writeAt( int fd, void * bufPtr, off_t offset, ssize_t length )
{
    int			blocksize;
    off_t		deviceoffset;
    ssize_t		bytestransferred;
    void *		rawData = NULL;
    off_t		rawOffset;
    ssize_t		rawLength;
    ssize_t		dataOffset = 0;
    int			result = FSUR_IO_SUCCESS;

    if (ioctl(fd, DKIOCBLKSIZE, &blocksize) < 0) {
#if TRACE_HFS_UTIL
    	fprintf(stderr, "hfs.util: couldn't determine block size of device.\n");
#endif
		result = FSUR_IO_FAIL;
		goto Return;
    }
    /* put offset and length in terms of device blocksize */
    rawOffset = offset / blocksize * blocksize;
    dataOffset = offset - rawOffset;
    rawLength = ((length + dataOffset + blocksize - 1) / blocksize) * blocksize;
    rawData = malloc(rawLength);
    if (rawData == NULL) {
		result = FSUR_IO_FAIL;
		goto Return;
    }

    deviceoffset = lseek( fd, rawOffset, SEEK_SET );
    if ( deviceoffset != rawOffset ) {
        result = FSUR_IO_FAIL;
        goto Return;
    }

	/* If the write isn't block-aligned, read the existing data before writing the new data: */
	if (((rawOffset % blocksize) != 0) || ((rawLength % blocksize) != 0)) {
		bytestransferred = read(fd, rawData, rawLength);
	    if ( bytestransferred != rawLength ) {
#if TRACE_HFS_UTIL
    		fprintf(stderr, "writeAt: attempt to pre-read data from device failed (errno = %d)\n", errno);
#endif
	        result = FSUR_IO_FAIL;
	        goto Return;
	    }
	};
	
    bcopy(bufPtr, rawData + dataOffset, length);	/* Copy in the new data */
    
    deviceoffset = lseek( fd, rawOffset, SEEK_SET );
    if ( deviceoffset != rawOffset ) {
        result = FSUR_IO_FAIL;
        goto Return;
    }

    bytestransferred = write(fd, rawData, rawLength);
    if ( bytestransferred != rawLength ) {
#if TRACE_HFS_UTIL
    		fprintf(stderr, "writeAt: attempt to write data to device failed?!");
#endif
        result = FSUR_IO_FAIL;
        goto Return;
    }

Return:
    if (rawData) free(rawData);

    return result;

} /* writeAt */


/******************************************************************************
 *
 *  V O L U M E   S T A T U S   D A T A B A S E   R O U T I N E S
 *
 *****************************************************************************/

#define DBHANDLESIGNATURE 0x75917737

/* Flag values for operation options: */
#define DBMARKPOSITION 1

static char gVSDBPath[] = "/var/db/volinfo.database";

#define MAXIOMALLOC 16384

/* Database layout: */

struct VSDBKey {
	char uuid[16];
};

struct VSDBRecord {
	char statusFlags[8];
};

struct VSDBEntry {
	struct VSDBKey key;
	char keySeparator;
	char space;
	struct VSDBRecord record;
	char terminator;
};

#define DBKEYSEPARATOR ':'
#define DBBLANKSPACE ' '
#define DBRECORDTERMINATOR '\n'

/* In-memory data structures: */

struct VSDBState {
    unsigned long signature;
    int dbfile;
    int dbmode;
    off_t recordPosition;
};

typedef struct VSDBState *VSDBStatePtr;

typedef struct {
    unsigned long state[5];
    unsigned long count[2];
    unsigned char buffer[64];
} SHA1_CTX;



/* Internal function prototypes: */
static int LockDB(VSDBStatePtr dbstateptr, int lockmode);
static int UnlockDB(VSDBStatePtr dbstateptr);

static int FindVolumeRecordByUUID(VSDBStatePtr dbstateptr, VolumeUUID *volumeID, struct VSDBEntry *dbentry, unsigned long options);
static int AddVolumeRecord(VSDBStatePtr dbstateptr, struct VSDBEntry *dbentry);
static int UpdateVolumeRecord(VSDBStatePtr dbstateptr, struct VSDBEntry *dbentry);
static int GetVSDBEntry(VSDBStatePtr dbstateptr, struct VSDBEntry *dbentry);
static int CompareVSDBKeys(struct VSDBKey *key1, struct VSDBKey *key2);

static void FormatULong(unsigned long u, char *s);
static void FormatUUID(VolumeUUID *volumeID, char *UUIDField);
static void FormatDBKey(VolumeUUID *volumeID, struct VSDBKey *dbkey);
static void FormatDBRecord(unsigned long volumeStatusFlags, struct VSDBRecord *dbrecord);
static void FormatDBEntry(VolumeUUID *volumeID, unsigned long volumeStatusFlags, struct VSDBEntry *dbentry);
static unsigned long ConvertHexStringToULong(const char *hs, long maxdigits);

static void SHA1Transform(unsigned long state[5], unsigned char buffer[64]);
static void SHA1Init(SHA1_CTX* context);
static void SHA1Update(SHA1_CTX* context, void* data, size_t len);
static void SHA1Final(unsigned char digest[20], SHA1_CTX* context);



/******************************************************************************
 *
 *  P U B L I S H E D   I N T E R F A C E   R O U T I N E S
 *
 *****************************************************************************/

void GenerateVolumeUUID(VolumeUUID *newVolumeID) {
	SHA1_CTX context;
	char randomInputBuffer[26];
	unsigned char digest[20];
	time_t now;
	clock_t uptime;
	int mib[2];
	int sysdata;
	char sysctlstring[128];
	size_t datalen;
	struct loadavg sysloadavg;
	struct vmtotal sysvmtotal;
	
	do {
		/* Initialize the SHA-1 context for processing: */
		SHA1Init(&context);
		
		/* Now process successive bits of "random" input to seed the process: */
		
		/* The current system's uptime: */
		uptime = clock();
		SHA1Update(&context, &uptime, sizeof(uptime));
		
		/* The kernel's boot time: */
		mib[0] = CTL_KERN;
		mib[1] = KERN_BOOTTIME;
		datalen = sizeof(sysdata);
		sysctl(mib, 2, &sysdata, &datalen, NULL, 0);
		SHA1Update(&context, &sysdata, datalen);
		
		/* The system's host id: */
		mib[0] = CTL_KERN;
		mib[1] = KERN_HOSTID;
		datalen = sizeof(sysdata);
		sysctl(mib, 2, &sysdata, &datalen, NULL, 0);
		SHA1Update(&context, &sysdata, datalen);

		/* The system's host name: */
		mib[0] = CTL_KERN;
		mib[1] = KERN_HOSTNAME;
		datalen = sizeof(sysctlstring);
		sysctl(mib, 2, sysctlstring, &datalen, NULL, 0);
		SHA1Update(&context, sysctlstring, datalen);

		/* The running kernel's OS release string: */
		mib[0] = CTL_KERN;
		mib[1] = KERN_OSRELEASE;
		datalen = sizeof(sysctlstring);
		sysctl(mib, 2, sysctlstring, &datalen, NULL, 0);
		SHA1Update(&context, sysctlstring, datalen);

		/* The running kernel's version string: */
		mib[0] = CTL_KERN;
		mib[1] = KERN_VERSION;
		datalen = sizeof(sysctlstring);
		sysctl(mib, 2, sysctlstring, &datalen, NULL, 0);
		SHA1Update(&context, sysctlstring, datalen);

		/* The system's load average: */
		mib[0] = CTL_VM;
		mib[1] = VM_LOADAVG;
		datalen = sizeof(sysloadavg);
		sysctl(mib, 2, &sysloadavg, &datalen, NULL, 0);
		SHA1Update(&context, &sysloadavg, datalen);

		/* The system's VM statistics: */
		mib[0] = CTL_VM;
		mib[1] = VM_METER;
		datalen = sizeof(sysvmtotal);
		sysctl(mib, 2, &sysvmtotal, &datalen, NULL, 0);
		SHA1Update(&context, &sysvmtotal, datalen);

		/* The current GMT (26 ASCII characters): */
		time(&now);
		strncpy(randomInputBuffer, asctime(gmtime(&now)), 26);	/* "Mon Mar 27 13:46:26 2000" */
		SHA1Update(&context, randomInputBuffer, 26);
		
		/* Pad the accumulated input and extract the final digest hash: */
		SHA1Final(digest, &context);
	
		memcpy(newVolumeID, digest, sizeof(*newVolumeID));
	} while ((newVolumeID->v.high == 0) || (newVolumeID->v.low == 0));
}



void ConvertVolumeUUIDStringToUUID(const char *UUIDString, VolumeUUID *volumeID) {
	int i;
	char c;
	unsigned long nextdigit;
	unsigned long high = 0;
	unsigned long low = 0;
	unsigned long carry;
	
	for (i = 0; (i < VOLUMEUUIDLENGTH) && ((c = UUIDString[i]) != (char)0) ; ++i) {
		if ((c >= '0') && (c <= '9')) {
			nextdigit = c - '0';
		} else if ((c >= 'A') && (c <= 'F')) {
			nextdigit = c - 'A' + 10;
		} else if ((c >= 'a') && (c <= 'f')) {
			nextdigit = c - 'a' + 10;
		} else {
			nextdigit = 0;
		};
		carry = ((low & 0xF0000000) >> 28) & 0x0000000F;
		high = (high << 4) | carry;
		low = (low << 4) | nextdigit;
	};
	
	volumeID->v.high = high;
	volumeID->v.low = low;
}



void ConvertVolumeUUIDToString(VolumeUUID *volumeID, char *UUIDString) {
	FormatUUID(volumeID, UUIDString);
	*(UUIDString+16) = (char)0;		/* Append a terminating null character */
}



int OpenVolumeStatusDB(VolumeStatusDBHandle *DBHandlePtr) {
	VSDBStatePtr dbstateptr;
	
	*DBHandlePtr = NULL;

	dbstateptr = (VSDBStatePtr)malloc(sizeof(*dbstateptr));
	if (dbstateptr == NULL) {
		return ENOMEM;
	};
	
	dbstateptr->dbmode = O_RDWR;
	dbstateptr->dbfile = open(gVSDBPath, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (dbstateptr->dbfile == -1) {
		/*
		   The file couldn't be opened for read/write access:
		   try read-only access before giving up altogether.
		 */
		dbstateptr->dbmode = O_RDONLY;
		dbstateptr->dbfile = open(gVSDBPath, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (dbstateptr->dbfile == -1) {
			return errno;
		};
	};
	
	dbstateptr->signature = DBHANDLESIGNATURE;
	*DBHandlePtr = (VolumeStatusDBHandle)dbstateptr;
	return 0;
}



int GetVolumeStatusDBEntry(VolumeStatusDBHandle DBHandle, VolumeUUID *volumeID, unsigned long *VolumeStatus) {
	VSDBStatePtr dbstateptr = (VSDBStatePtr)DBHandle;
	struct VSDBEntry dbentry;
	int result;

	if (dbstateptr->signature != DBHANDLESIGNATURE) return EINVAL;

	if ((result = LockDB(dbstateptr, LOCK_SH)) != 0) return result;
	
	if ((result = FindVolumeRecordByUUID(dbstateptr, volumeID, &dbentry, 0)) != 0) {
		goto ErrExit;
	};
	*VolumeStatus = VOLUME_RECORDED | ConvertHexStringToULong(dbentry.record.statusFlags, sizeof(dbentry.record.statusFlags));
	
	result = 0;

ErrExit:
	UnlockDB(dbstateptr);
	return result;
}



int SetVolumeStatusDBEntry(VolumeStatusDBHandle DBHandle, VolumeUUID *volumeID, unsigned long VolumeStatus) {
	VSDBStatePtr dbstateptr = (VSDBStatePtr)DBHandle;
	struct VSDBEntry dbentry;
	int result;
	
	if (dbstateptr->signature != DBHANDLESIGNATURE) return EINVAL;
	if (VolumeStatus & ~VOLUME_VALIDSTATUSBITS) return EINVAL;
	
	if ((result = LockDB(dbstateptr, LOCK_EX)) != 0) return result;
	
	FormatDBEntry(volumeID, VolumeStatus, &dbentry);
	if ((result = FindVolumeRecordByUUID(dbstateptr, volumeID, NULL, DBMARKPOSITION)) == 0) {
#if DEBUG_TRACE
		fprintf(stderr,"AddLocalVolumeUUID: found record in database; updating in place.\n");
#endif
		result = UpdateVolumeRecord(dbstateptr, &dbentry);
	} else if (result == -1) {
#if DEBUG_TRACE
		fprintf(stderr,"AddLocalVolumeUUID: record not found in database; appending at end.\n");
#endif
		result = AddVolumeRecord(dbstateptr, &dbentry);
	} else {
		goto ErrExit;
	};
	
	fsync(dbstateptr->dbfile);
	
	result = 0;

ErrExit:	
	UnlockDB(dbstateptr);
	return result;
}



int DeleteVolumeStatusDBEntry(VolumeStatusDBHandle DBHandle, VolumeUUID *volumeID) {
	VSDBStatePtr dbstateptr = (VSDBStatePtr)DBHandle;
	struct stat dbinfo;
	int result;
	unsigned long iobuffersize;
	void *iobuffer = NULL;
	off_t dataoffset;
	unsigned long iotransfersize;
	unsigned long bytestransferred;

	if (dbstateptr->signature != DBHANDLESIGNATURE) return EINVAL;

	if ((result = LockDB(dbstateptr, LOCK_EX)) != 0) return result;
	
	if ((result = FindVolumeRecordByUUID(dbstateptr, volumeID, NULL, DBMARKPOSITION)) != 0) {
#if DEBUG_TRACE
		fprintf(stderr, "DeleteLocalVolumeUUID: No record with matching volume UUID in DB (result = %d).\n", result);
#endif
		if (result == -1) result = 0;	/* Entry wasn't in the database to begin with? */
		goto StdEdit;
	} else {
#if DEBUG_TRACE
		fprintf(stderr, "DeleteLocalVolumeUUID: Found record with matching volume UUID...\n");
#endif
		if ((result = stat(gVSDBPath, &dbinfo)) != 0) goto ErrExit;
		if ((dbinfo.st_size - dbstateptr->recordPosition - sizeof(struct VSDBEntry)) <= MAXIOMALLOC) {
			iobuffersize = dbinfo.st_size - dbstateptr->recordPosition - sizeof(struct VSDBEntry);
		} else {
			iobuffersize = MAXIOMALLOC;
		};
#if DEBUG_TRACE
		fprintf(stderr, "DeleteLocalVolumeUUID: DB size = 0x%08lx; recordPosition = 0x%08lx;\n", 
							(unsigned long)dbinfo.st_size, (unsigned long)dbstateptr->recordPosition);
		fprintf(stderr, "DeleteLocalVolumeUUID: I/O buffer size = 0x%lx\n", iobuffersize);
#endif
		if (iobuffersize > 0) {
			iobuffer = malloc(iobuffersize);
			if (iobuffer == NULL) {
				result = ENOMEM;
				goto ErrExit;
			};
			
			dataoffset = dbstateptr->recordPosition + sizeof(struct VSDBEntry);
			do {
				iotransfersize = dbinfo.st_size - dataoffset;
				if (iotransfersize > 0) {
					if (iotransfersize > iobuffersize) iotransfersize = iobuffersize;
	
	#if DEBUG_TRACE
					fprintf(stderr, "DeleteLocalVolumeUUID: reading 0x%08lx bytes starting at 0x%08lx ...\n", iotransfersize, (unsigned long)dataoffset);
	#endif
					lseek(dbstateptr->dbfile, dataoffset, SEEK_SET);
					bytestransferred = read(dbstateptr->dbfile, iobuffer, iotransfersize);
					if (bytestransferred != iotransfersize) {
						result = errno;
						goto ErrExit;
					};
	
	#if DEBUG_TRACE
					fprintf(stderr, "DeleteLocalVolumeUUID: writing 0x%08lx bytes starting at 0x%08lx ...\n", iotransfersize, (unsigned long)(dataoffset - (off_t)sizeof(struct VSDBEntry)));
	#endif
					lseek(dbstateptr->dbfile, dataoffset - (off_t)sizeof(struct VSDBEntry), SEEK_SET);
					bytestransferred = write(dbstateptr->dbfile, iobuffer, iotransfersize);
					if (bytestransferred != iotransfersize) {
						result = errno;
						goto ErrExit;
					};
					
					dataoffset += (off_t)iotransfersize;
				};
			} while (iotransfersize > 0);
		};
#if DEBUG_TRACE
		fprintf(stderr, "DeleteLocalVolumeUUID: truncating database file to 0x%08lx bytes.\n", (unsigned long)(dbinfo.st_size - (off_t)(sizeof(struct VSDBEntry))));
#endif
		if ((result = ftruncate(dbstateptr->dbfile, dbinfo.st_size - (off_t)(sizeof(struct VSDBEntry)))) != 0) {
			goto ErrExit;
		};
		
		fsync(dbstateptr->dbfile);
		
		result = 0;
	};

ErrExit:
	if (iobuffer) free(iobuffer);
	UnlockDB(dbstateptr);
	
StdEdit:
	return result;
}



int CloseVolumeStatusDB(VolumeStatusDBHandle DBHandle) {
	VSDBStatePtr dbstateptr = (VSDBStatePtr)DBHandle;

	if (dbstateptr->signature != DBHANDLESIGNATURE) return EINVAL;

	dbstateptr->signature = 0;
	
	close(dbstateptr->dbfile);		/* Nothing we can do about any errors... */
	dbstateptr->dbfile = 0;
	
	free(dbstateptr);
	
	return 0;
}



/******************************************************************************
 *
 *  I N T E R N A L   O N L Y   D A T A B A S E   R O U T I N E S
 *
 *****************************************************************************/

static int LockDB(VSDBStatePtr dbstateptr, int lockmode) {
#if DEBUG_TRACE
	fprintf(stderr, "LockDB: Locking VSDB file...\n");
#endif
	return flock(dbstateptr->dbfile, lockmode);
}

	

static int UnlockDB(VSDBStatePtr dbstateptr) {
#if DEBUG_TRACE
	fprintf(stderr, "UnlockDB: Unlocking VSDB file...\n");
#endif
	return flock(dbstateptr->dbfile, LOCK_UN);
}



static int FindVolumeRecordByUUID(VSDBStatePtr dbstateptr, VolumeUUID *volumeID, struct VSDBEntry *targetEntry, unsigned long options) {
	struct VSDBKey searchkey;
	struct VSDBEntry dbentry;
	int result;
	
	FormatDBKey(volumeID, &searchkey);
	lseek(dbstateptr->dbfile, 0, SEEK_SET);
	
	do {
		result = GetVSDBEntry(dbstateptr, &dbentry);
		if ((result == 0) && (CompareVSDBKeys(&dbentry.key, &searchkey) == 0)) {
			if (targetEntry != NULL) {
#if DEBUG_TRACE
				fprintf(stderr, "FindVolumeRecordByUUID: copying %d. bytes from %08xl to %08l...\n", sizeof(*targetEntry), &dbentry, targetEntry);
#endif
				memcpy(targetEntry, &dbentry, sizeof(*targetEntry));
			};
			return 0;
		};
	} while (result == 0);
	
	return -1;
}



static int AddVolumeRecord(VSDBStatePtr dbstateptr , struct VSDBEntry *dbentry) {
#if DEBUG_TRACE
	VolumeUUIDString id;
#endif

#if DEBUG_TRACE
	strncpy(id, dbentry->key.uuid, sizeof(dbentry->key.uuid));
	id[sizeof(dbentry->key.uuid)] = (char)0;
	fprintf(stderr, "AddVolumeRecord: Adding record for UUID #%s...\n", id);
#endif
	lseek(dbstateptr->dbfile, 0, SEEK_END);
	return write(dbstateptr->dbfile, dbentry, sizeof(struct VSDBEntry));
}




static int UpdateVolumeRecord(VSDBStatePtr dbstateptr, struct VSDBEntry *dbentry) {
#if DEBUG_TRACE
	VolumeUUIDString id;
#endif

#if DEBUG_TRACE
	strncpy(id, dbentry->key.uuid, sizeof(dbentry->key.uuid));
	id[sizeof(dbentry->key.uuid)] = (char)0;
	fprintf(stderr, "UpdateVolumeRecord: Updating record for UUID #%s at offset 0x%08lx in database...\n", id, (unsigned long)dbstateptr->recordPosition);
#endif
	lseek(dbstateptr->dbfile, dbstateptr->recordPosition, SEEK_SET);
#if DEBUG_TRACE
	fprintf(stderr, "UpdateVolumeRecord: Writing %d. bytes...\n", sizeof(*dbentry));
#endif
	return write(dbstateptr->dbfile, dbentry, sizeof(*dbentry));
}



static int GetVSDBEntry(VSDBStatePtr dbstateptr, struct VSDBEntry *dbentry) {
	struct VSDBEntry entry;
	int result;
#if DEBUG_TRACE
	VolumeUUIDString id;
#endif
	
	dbstateptr->recordPosition = lseek(dbstateptr->dbfile, 0, SEEK_CUR);
#if 0 // DEBUG_TRACE
	fprintf(stderr, "GetVSDBEntry: starting reading record at offset 0x%08lx...\n", (unsigned long)dbstateptr->recordPosition);
#endif
	result = read(dbstateptr->dbfile, &entry, sizeof(entry));
	if ((result != sizeof(entry)) ||
		(entry.keySeparator != DBKEYSEPARATOR) ||
		(entry.space != DBBLANKSPACE) ||
		(entry.terminator != DBRECORDTERMINATOR)) {
		return -1;
	};
	
#if DEBUG_TRACE
	strncpy(id, entry.key.uuid, sizeof(entry.key.uuid));
	id[sizeof(entry.key.uuid)] = (char)0;
	fprintf(stderr, "GetVSDBEntry: returning entry for UUID #%s...\n", id);
#endif
	memcpy(dbentry, &entry, sizeof(*dbentry));
	return 0;
};



static int CompareVSDBKeys(struct VSDBKey *key1, struct VSDBKey *key2) {
#if 0 // DEBUG_TRACE
	VolumeUUIDString id;

	strncpy(id, key1->uuid, sizeof(key1->uuid));
	id[sizeof(key1->uuid)] = (char)0;
	fprintf(stderr, "CompareVSDBKeys: comparing #%s to ", id);
	strncpy(id, key2->uuid, sizeof(key2->uuid));
	id[sizeof(key2->uuid)] = (char)0;
	fprintf(stderr, "%s (%d.)...\n", id, sizeof(key1->uuid));
#endif
	
	return memcmp(key1->uuid, key2->uuid, sizeof(key1->uuid));
}



/******************************************************************************
 *
 *  F O R M A T T I N G   A N D   C O N V E R S I O N   R O U T I N E S
 *
 *****************************************************************************/

static void FormatULong(unsigned long u, char *s) {
	unsigned long d;
	int i;
	char *digitptr = s;

	for (i = 0; i < 8; ++i) {
		d = ((u & 0xF0000000) >> 28) & 0x0000000F;
		if (d < 10) {
			*digitptr++ = (char)(d + '0');
		} else {
			*digitptr++ = (char)(d - 10 + 'A');
		};
		u = u << 4;
	};
}



static void FormatUUID(VolumeUUID *volumeID, char *UUIDField) {
	FormatULong(volumeID->v.high, UUIDField);
	FormatULong(volumeID->v.low, UUIDField+8);

};



static void FormatDBKey(VolumeUUID *volumeID, struct VSDBKey *dbkey) {
	FormatUUID(volumeID, dbkey->uuid);
}



static void FormatDBRecord(unsigned long volumeStatusFlags, struct VSDBRecord *dbrecord) {
	FormatULong(volumeStatusFlags, dbrecord->statusFlags);
}



static void FormatDBEntry(VolumeUUID *volumeID, unsigned long volumeStatusFlags, struct VSDBEntry *dbentry) {
	FormatDBKey(volumeID, &dbentry->key);
	dbentry->keySeparator = DBKEYSEPARATOR;
	dbentry->space = DBBLANKSPACE;
	FormatDBRecord(volumeStatusFlags, &dbentry->record);
#if 0 // DEBUG_TRACE
	dbentry->terminator = (char)0;
	fprintf(stderr, "FormatDBEntry: '%s' (%d.)\n", dbentry, sizeof(*dbentry));
#endif
	dbentry->terminator = DBRECORDTERMINATOR;
}



static unsigned long ConvertHexStringToULong(const char *hs, long maxdigits) {
	int i;
	char c;
	unsigned long nextdigit;
	unsigned long n;
	
	n = 0;
	for (i = 0; (i < 8) && ((c = hs[i]) != (char)0) ; ++i) {
		if ((c >= '0') && (c <= '9')) {
			nextdigit = c - '0';
		} else if ((c >= 'A') && (c <= 'F')) {
			nextdigit = c - 'A' + 10;
		} else if ((c >= 'a') && (c <= 'f')) {
			nextdigit = c - 'a' + 10;
		} else {
			nextdigit = 0;
		};
		n = (n << 4) + nextdigit;
	};
	
	return n;
}



/******************************************************************************
 *
 *  S H A - 1   I M P L E M E N T A T I O N   R O U T I N E S
 *
 *****************************************************************************/

/*
	Derived from SHA-1 in C
	By Steve Reid <steve@edmweb.com>
	100% Public Domain

	Test Vectors (from FIPS PUB 180-1)
		"abc"
			A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
			84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
		A million repetitions of "a"
			34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/

/* #define LITTLE_ENDIAN * This should be #define'd if true. */
/* #define SHA1HANDSOFF * Copies data before messing with it. */

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#ifdef LITTLE_ENDIAN
#define blk0(i) (block->l[i] = (rol(block->l[i],24)&0xFF00FF00) \
    |(rol(block->l[i],8)&0x00FF00FF))
#else
#define blk0(i) block->l[i]
#endif
#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] \
    ^block->l[(i+2)&15]^block->l[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#if TRACE_HASH
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);printf("t = %2d: %08lX %08lX %08lX %08lX %08lX\n", i, a, b, c, d, e);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);printf("t = %2d: %08lX %08lX %08lX %08lX %08lX\n", i, a, b, c, d, e);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);printf("t = %2d: %08lX %08lX %08lX %08lX %08lX\n", i, a, b, c, d, e);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);printf("t = %2d: %08lX %08lX %08lX %08lX %08lX\n", i, a, b, c, d, e);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);printf("t = %2d: %08lX %08lX %08lX %08lX %08lX\n", i, a, b, c, d, e);
#else
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);
#endif


/* Hash a single 512-bit block. This is the core of the algorithm. */

static void SHA1Transform(unsigned long state[5], unsigned char buffer[64])
{
unsigned long a, b, c, d, e;
typedef union {
    unsigned char c[64];
    unsigned long l[16];
} CHAR64LONG16;
CHAR64LONG16* block;
#ifdef SHA1HANDSOFF
static unsigned char workspace[64];
    block = (CHAR64LONG16*)workspace;
    memcpy(block, buffer, 64);
#else
    block = (CHAR64LONG16*)buffer;
#endif
    /* Copy context->state[] to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
#if TRACE_HASH
    printf("            A        B        C        D        E\n");
    printf("        -------- -------- -------- -------- --------\n");
    printf("        %08lX %08lX %08lX %08lX %08lX\n", a, b, c, d, e);
#endif
    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);
    /* Add the working vars back into context.state[] */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    /* Wipe variables */
    a = b = c = d = e = 0;
}


/* SHA1Init - Initialize new context */

static void SHA1Init(SHA1_CTX* context)
{
    /* SHA1 initialization constants */
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}


/* Run your data through this. */

static void SHA1Update(SHA1_CTX* context, void* data, size_t len)
{
	unsigned char *dataptr = (char *)data;
	unsigned int i, j;

    j = (context->count[0] >> 3) & 63;
    if ((context->count[0] += len << 3) < (len << 3)) context->count[1]++;
    context->count[1] += (len >> 29);
    if ((j + len) > 63) {
        memcpy(&context->buffer[j], dataptr, (i = 64-j));
        SHA1Transform(context->state, context->buffer);
        for ( ; i + 63 < len; i += 64) {
            SHA1Transform(context->state, &dataptr[i]);
        }
        j = 0;
    }
    else i = 0;
    memcpy(&context->buffer[j], &dataptr[i], len - i);
}


/* Add padding and return the message digest. */

static void SHA1Final(unsigned char digest[20], SHA1_CTX* context)
{
unsigned long i, j;
unsigned char finalcount[8];

    for (i = 0; i < 8; i++) {
        finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)]
         >> ((3-(i & 3)) * 8) ) & 255);  /* Endian independent */
    }
    SHA1Update(context, (unsigned char *)"\200", 1);
    while ((context->count[0] & 504) != 448) {
        SHA1Update(context, (unsigned char *)"\0", 1);
    }
    SHA1Update(context, finalcount, 8);  /* Should cause a SHA1Transform() */
    for (i = 0; i < 20; i++) {
        digest[i] = (unsigned char)
         ((context->state[i>>2] >> ((3-(i & 3)) * 8) ) & 255);
    }
    /* Wipe variables */
    i = j = 0;
    memset(context->buffer, 0, 64);
    memset(context->state, 0, 20);
    memset(context->count, 0, 8);
    memset(&finalcount, 0, 8);
#ifdef SHA1HANDSOFF  /* make SHA1Transform overwrite it's own static vars */
    SHA1Transform(context->state, context->buffer);
#endif
}
