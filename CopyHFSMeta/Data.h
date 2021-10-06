#ifndef _DATA_H
# define _DATA_H

/*
 * Data-manipulating functions and structures, used to
 * create the skeleton copy.
 */
struct DeviceInfo;
struct VolumeDescriptor;
struct IOWrapper;

/*
 * We treat each data structure in the filesystem as
 * a <start, length> pair.
 */
struct Extents {
	off_t base;
	off_t length;
};
typedef struct Extents Extents_t;

#define kExtentCount	100

/*
 * The in-core representation consists of a linked
 * list of an array of extents, up to 100 in each element.
 */
struct ExtentList {
	size_t count;
	Extents_t extents[kExtentCount];
	struct ExtentList *next;
};
typedef struct ExtentList ExtentList_t;

/*
 * The in-core description of the volume:  an input source,
 * a description of the volume, the linked list of extents,
 * the total number of bytes, and the number of linked list
 * elements.
 */
struct VolumeObjects {
	struct DeviceInfo *devp;
	struct VolumeDescriptor *vdp;
	size_t count;
	off_t byteCount;
	ExtentList_t *list;
};
typedef struct VolumeObjects VolumeObjects_t;

extern VolumeObjects_t *InitVolumeObject(struct DeviceInfo *devp, struct VolumeDescriptor *vdp);
extern int AddExtent(VolumeObjects_t *vop, off_t start, off_t length);
extern void PrintVolumeObject(VolumeObjects_t*);
extern int CopyObjectsToDest(VolumeObjects_t*, struct IOWrapper *wrapper, off_t skip);

extern void WriteGatheredData(const char *, VolumeObjects_t*);

#endif /* _DATA_H */
