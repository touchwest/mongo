/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2008 WiredTiger Software.
 *	All rights reserved.
 *
 * $Id$
 */

#if defined(__cplusplus)
extern "C" {
#endif

/*******************************************
 * Internal forward declarations.
 *******************************************/
struct __wt_item;		typedef struct __wt_item WT_ITEM;
struct __wt_off;		typedef struct __wt_off WT_OFF;
struct __wt_ovfl;		typedef struct __wt_ovfl WT_OVFL;
struct __wt_page_desc;		typedef struct __wt_page_desc WT_PAGE_DESC;
struct __wt_page_hdr;		typedef struct __wt_page_hdr WT_PAGE_HDR;
struct __wt_repl;		typedef struct __wt_repl WT_REPL;

/*
 * We use 32-bits to store file locations on database pages, so all such file
 * locations are counts of "database allocation units" (making an "allocation
 * unit" the smallest database chunk allocated from an underlying file).  In
 * the code, these are all "addresses" or "addrs".  To simplify bookkeeping,
 * database internal and leaf page sizes, and extent size, must be a multiple
 * of the allocation unit size.
 *
 * The minimum database allocation unit is 512B so the minimum maximum database
 * size is 2TB, and the maximum maximum (assuming we could pass file offsets
 * that large, which we can't), is 4EB.   In summary, small allocation units
 * limit the database size, and as the allocation unit grows, the maximum size
 * of the database grows as well.
 *
 * Underneath the database layer is the cache and file layers.  In both, sizes
 * are stored as numbers of bytes.   In the cache layer, 32-bits is too small
 * (a cache might be larger than 4GB), so we use a 64-bit type.  In the file
 * layer, 32-bits might also be too small, but we have a standard type known to
 * hold the size of a file, an off_t.
 */
/* Convert a data address to/from a byte offset. */
#define	WT_ADDR_TO_OFF(db, addr)					\
	((off_t)(addr) * (db)->allocsize)
#define	WT_OFF_TO_ADDR(db, off)						\
	((u_int32_t)((off) / (db)->allocsize))

/* Return bytes needed for an overflow item, rounded to an allocation unit. */
#define	WT_OVFL_BYTES(db, len)						\
	((u_int32_t)WT_ALIGN((len) + sizeof(WT_PAGE_HDR), (db)->allocsize))

/*
 * The first possible address is 0.  It is also always the first leaf page in
 * in the database because it's created first and never replaced.
 *
 * The invalid address is the largest possible offset, which isn't a possible
 * database address.
 */
#define	WT_ADDR_FIRST_PAGE	0
#define	WT_ADDR_INVALID		UINT32_MAX

/*
 * The database itself needs a chunk of memory that describes it.   Here's
 * the structure.
 *
 * !!!
 * Field order is important: there's a 8-byte type in the middle, and the
 * Solaris compiler inserts space into the structure if we don't put that
 * field on an 8-byte boundary.
 */
struct __wt_page_desc {
#define	WT_BTREE_MAGIC		0x120897
	u_int32_t magic;		/* 00-03: Magic number */
#define	WT_BTREE_MAJOR_VERSION	0
	u_int16_t majorv;		/* 04-05: Major version */
#define	WT_BTREE_MINOR_VERSION	1
	u_int16_t minorv;		/* 06-07: Minor version */
	u_int32_t leafsize;		/* 08-11: Leaf page size */
	u_int32_t intlsize;		/* 12-15: Internal page size */
	u_int64_t base_recno;		/* 16-23: Base record number */
	u_int32_t root_addr;		/* 24-27: Root address */
	u_int32_t free_addr;		/* 28-31: Freelist address */

#define	WT_PAGE_DESC_REPEAT	0x01	/* Repeat count compression */
#define	WT_PAGE_DESC_MASK	0x01	/* Valid bit mask */
	u_int32_t flags;		/* 32-35: Flags */

	u_int8_t  fixed_len;		/* 36:	  Fixed length byte count */
	u_int8_t  unused1[3];		/* 37-39: Spare */

	u_int32_t unused2[6];		/* 40-63: Spare */
};
/*
 * WT_PAGE_DESC_SIZE is the expected structure size --  we check at startup to
 * ensure the compiler hasn't inserted padding (which would break the world).
 */
#define	WT_PAGE_DESC_SIZE		64

/*
 * WT_PAGE --
 * The WT_PAGE structure describes the in-memory information about a database
 * page.   When pages are read, the page is reviewed, and in-memory specific
 * information is created.  That information is generally what's used in the
 * cache, not the actual page itself.
 */
struct __wt_page {
	WT_PAGE  *next;			/* Hash queue */
	u_int32_t page_gen;		/* LRU generation number */

	DB	 *db;			/* Page's backing database */
	u_int32_t addr;			/* Page's allocation address */

	/*
	 * The page size is limited to 4GB by this type -- we could use off_t's
	 * here if we need something bigger, but the page-size configuration
	 * code limits page sizes to 128MB.
	 */
	u_int32_t bytes;		/* Page size */

	WT_PAGE_HDR *hdr;		/* Page's on-disk representation */

	u_int8_t *first_free;		/* Page's first free byte address */
	u_int32_t space_avail;		/* Page's available memory */

	u_int64_t records;		/* Records in this page and below */

	/*
	 * Each item on a page is referenced by a the indx field, which points
	 * to a WT_ROW_INDX or WT_COL_INDX structure.  (This is where the
	 * on-page index array found in DB 1.85 and Berkeley DB moved.)  The
	 * indx field initially references an array of WT_{ROW,COL}_INDX
	 * structures.  If a record on the page is modified, the indx field
	 * changes to reference a binary tree that references WT_{ROW,COL}_INDX
	 * structures.
	 *
	 * We use a union so you can increment the address and have the right
	 * thing happen (and so you're forced to think about what exactly you
	 * are dereferencing when you write the code).
	 */
	union {				/* Entry index */
		struct __wt_col_indx *c_indx;
		struct __wt_row_indx *r_indx;
		void *indx;
	} u;
	u_int32_t indx_count;		/* Entry count */

	/*
	 * The drain field is set when the page is being discarded from the
	 * cache.   Threads acquiring pages must set a hazard pointer before
	 * checking the drain field, the cache server must set the drain field
	 * before checking the hazard pointers.
	 */
	u_int32_t drain;		/* Page is being actively drained */

	u_int32_t flags;
};
/*
 * WT_PAGE_SIZE is the expected structure size --  we check at startup to ensure
 * the compiler hasn't inserted padding.  The WT_PAGE structure is in-memory, so
 * padding it won't break the world, but we don't want to waste space, and there
 * are a lot of these structures.
 */
#define	WT_PAGE_SIZE		56

/*
 * WT_PAGE_HDR --
 *
 * All on-disk database pages have a common header, declared as the WT_PAGE_HDR
 * structure.  The header has no version number or mode bits, and the page type
 * and/or flags value will have to be modified when changes are made to the page
 * layout.  (The page type appears early in the header to make this simpler.)
 * In other words, the page type declares the contents of the page and how to
 * read it.
 *
 * For more information on page layouts and types, see the file btree_layout.
 */
struct __wt_page_hdr {
	u_int32_t lsn[2];		/* 00-07: LSN */

	u_int32_t checksum;		/* 08-11: checksum */

#define	WT_PAGE_INVALID		0	/* Invalid page */
#define	WT_PAGE_COL_FIX		1	/* Col store fixed-length leaf page */
#define	WT_PAGE_COL_INT		2	/* Col store internal page */
#define	WT_PAGE_COL_VAR		3	/* Col store var-length leaf page */
#define	WT_PAGE_DUP_INT		4	/* Duplicate tree internal page */
#define	WT_PAGE_DUP_LEAF	5	/* Duplicate tree leaf page */
#define	WT_PAGE_OVFL		6	/* Overflow page */
#define	WT_PAGE_ROW_INT		7	/* Row-store internal page */
#define	WT_PAGE_ROW_LEAF	8	/* Row-store leaf page */
	u_int8_t type;			/* 12: page type */

	/*
	 * Currently unused -- in the middle so we can grab the top byte for
	 * another field, and the bottom byte for more flags without getting
	 * crazy.
	 */
	u_int8_t unused[2];		/* 13-14: unused padding */

	/*
	 * WT_OFFPAGE_REF_LEAF --
	 *	Used on WT_PAGE_COL_INT pages (column-store internal pages),
	 *	specifies if offpage references are to internal pages or to
	 *	leaf pages.  See note at __wt_off structure.
	 */
#define	WT_OFFPAGE_REF_LEAF	0x01	/* Offpage structures reference leaf */
	u_int8_t flags;			/* 15: flags */

	union {
		u_int32_t datalen;	/* 16-19: overflow data length */
		u_int32_t entries;	/* 16-19: number of items on page */
	} u;

	/*
	 * Parent, forward and reverse page links.  Pages are linked at their
	 * level, that is, all the main btree leaf pages are linked, each set
	 * of offpage duplicate leaf pages are linked, and each level of
	 * internal pages are linked.
	 */
	u_int32_t prntaddr;		/* 20-23: parent page */
	u_int32_t prevaddr;		/* 24-27: previous page */
	u_int32_t nextaddr;		/* 28-31: next page */
};
/*
 * WT_PAGE_HDR_SIZE is the expected structure size --  we check at startup to
 * ensure the compiler hasn't inserted padding (which would break the world).
 * The size must be a multiple of a 4-byte boundary.
 *
 * It would be possible to reduce this by two bytes, by moving the odd-sized
 * fields to the end of the structure (and using something other than "sizeof"
 * for the check, as compilers usually pad the sizeof() operator to the next
 * 4-byte boundary), but I don't think two bytes are worth the effort.
 */
#define	WT_PAGE_HDR_SIZE		32

/*
 * WT_PAGE_BYTE is the first usable data byte on the page.  Note the correction
 * for page addr of 0, the first address.   It would be simpler to put this at
 * the end of the page, but that would make it more difficult to figure out the
 * page size in a just opened database.
 */
#define	WT_PAGE_BYTE(page)						\
	(((u_int8_t *)(page)->hdr) +					\
	WT_PAGE_HDR_SIZE + ((page)->addr == 0 ? WT_PAGE_DESC_SIZE : 0))

/*
 * WT_SDBT --
 *	A minimal version of the DBT structure -- just the data & size fields.
 */
typedef struct __wt_sdbt {
	void	 *data;			/* DBT: data */
	u_int32_t size;			/* DBT: data length */
} WT_SDBT;

/*
 * WT_REPL --
 *	A data replacement structure.
 */
struct __wt_repl {
	/*
	 * Data items on leaf pages may be updated with new data, stored in
	 * the WT_REPL structure.  It's an array for two reasons: first, we
	 * don't block readers when updating it, which means it may be in
	 * use during updates, and second because we'll need history when we
	 * add MVCC to the system.
	 */
#define	WT_DATA_DELETED	((void *)0x01)	/* Data item was deleted */
	WT_SDBT  *data;			/* Data array */

	u_int16_t repl_size;		/* Data array size */
	u_int16_t repl_next;		/* Next available slot */

	WT_REPL *next;			/* Previous replace structures */
};

/*
 * WT_ROW_INDX --
 * The WT_ROW_INDX structure describes the in-memory information about a single
 * key/data pair on a row-store database page.
 */
typedef	struct __wt_row_indx {
	/*
	 * WT_ROW_INDX structures are used to describe pages where there's a
	 * sort key (that is, a row-store, not a column-store, which is only
	 * "sorted" by record number).
	 *
	 * The first fields of the WT_ROW_INDX structure is the same as the
	 * first fields of a DBT so we can feed it to a comparison function
	 * without copying.  This is important for keys on internal pages.
	 */
	void	 *data;			/* DBT: data */
	u_int32_t size;			/* DBT: data length */

	void	 *page_data;		/* Original on-page data */

	WT_REPL	 *repl;			/* Replacement data array */

	u_int32_t flags;
} WT_ROW_INDX;
/*
 * WT_ROW_SIZE is the expected structure size --  we check at startup to ensure
 * the compiler hasn't inserted padding.  The WT_ROW structure is in-memory, so
 * padding it won't break the world, but we don't want to waste space, and there
 * are a lot of these structures.
 */
#define	WT_ROW_INDX_SIZE	20

/*
 * WT_COL_INDX --
 * The WT_COL_INDX structure describes the in-memory information about a single
 * item on a column-store database page.
 */
typedef	struct __wt_col_indx {
	void	 *page_data;		/* Original on-page data */
	WT_REPL	 *repl;			/* Replacement data array */
} WT_COL_INDX;
/*
 * WT_COL_SIZE is the expected structure size --  we check at startup to ensure
 * the compiler hasn't inserted padding.  The WT_COL structure is in-memory, so
 * padding it won't break the world, but we don't want to waste space, and there
 * are a lot of these structures.
 */
#define	WT_COL_INDX_SIZE	8

/* Macro to walk the indexes of an in-memory page. */
#define	WT_INDX_FOREACH(page, ip, i)					\
	for ((i) = (page)->indx_count,					\
	    (ip) = (page)->u.indx; (i) > 0; ++(ip), --(i))

/*
 *
 * Overflow and/or compressed on-page items need processing before we look at
 * them.   Handy macro to identify such.
 */
#define	WT_ROW_INDX_PROCESS(ip)						\
	((ip)->data == NULL || F_ISSET(ip, WT_HUFFMAN))

/*
 * On both row- and column-store internal pages, the on-page data referenced
 * by the WT_INDX page_data field is a WT_OFF structure, which contains a
 * record count and a page address.   These macros reach into the on-page
 * structure and return the values.
 */
#define	WT_COL_OFF_ADDR(ip)						\
	(((WT_OFF *)((ip)->page_data))->addr)
#define	WT_COL_OFF_RECORDS(ip)						\
	WT_RECORDS((WT_OFF *)((ip)->page_data))

#define	WT_ROW_OFF_ADDR(ip)						\
	(((WT_OFF *)WT_ITEM_BYTE((ip)->page_data))->addr)
#define	WT_ROW_OFF_RECORDS(ip)						\
	WT_RECORDS((WT_OFF *)WT_ITEM_BYTE((ip)->page_data))

/*
 * WT_ITEM --
 *	Trailing data length (in bytes) plus item type.
 *
 * After the page header, on pages with variable-length data, there is
 * Pages with variable-length items (all page types except for WT_PAGE_COL_INT
 * and WT_PAGE_COL_LEAF_FIXED), are comprised of a list of WT_ITEMs in sorted
 * order.  Or, specifically, 4 bytes followed by a variable length chunk.
 *
 * The first 8 bits of that 4 bytes holds an item type, followed by an item
 * length.  The item type defines the following set of bytes and the item
 * length specifies how long the item is.
 *
 * We encode the length and type in a 4-byte value to minimize the on-page
 * footprint as well as maintain alignment of the bytes that follow the item.
 * (The trade-off is this limits on-page database key or data items to 16MB.)
 * The bottom 24-bits are the length of the subsequent data, the next 4-bits are
 * the type, and the top 4-bits are unused.   We could use the unused 4-bits to
 * provide more length, but 16MB seems sufficient for on-page items.
 *
 * The __item_chunk field should never be directly accessed, there are macros
 * to extract the type and length.
 *
 * WT_ITEMs are aligned to a 4-byte boundary, so it's OK to directly access the
 * __item_chunk field on the page.
 */
#define	WT_ITEM_MAX_LEN	(16 * 1024 * 1024 - 1)
struct __wt_item {
	u_int32_t __item_chunk;
};
/*
 * WT_ITEM_SIZE is the expected structure size --  we check at startup to make
 * sure the compiler hasn't inserted padding (which would break the world).
 */
#define	WT_ITEM_SIZE	4

/*
 * There are 3 basic types: keys, data items and duplicate data items, each of
 * which has an overflow form.  The item is followed by additional data, which
 * varies by type: a key, data or duplicate item is followed by a set of bytes;
 * a WT_OVFL structure follows an overflow form.
 *
 * We could compress the item types (for example, use a bit to mean overflow),
 * but it's simpler this way because we don't need the page type to know what
 * "WT_ITEM_KEY" really means.  We express the item types as bit masks because
 * it makes the macro for assignment faster, but they are integer values, not
 * unique bits.
 *
 * Here's the usage by page type:
 *
 * WT_PAGE_ROW_INT (row-store internal pages):
 * Variable-length key and offpage-reference pairs (a WT_ITEM_KEY/KEY_OVFL item
 * followed by a WT_ITEM_OFF_INT/OFF_LEAF item).
 *
 * WT_PAGE_ROW_LEAF (row-store primary leaf pages):
 * -- Variable-length key followed by a single variable-length/data item (a
 *    WT_ITEM_KEY/KEY_OVFL item followed by a WT_ITEM_DATA/DATA_OVFL item);
 * -- Variable-length key/offpage-reference pairs (a WT_ITEM_KEY/KEY_OVFL item
 *    followed by a WT_ITEM_OFF_INT/OFF_LEAF item);
 * -- Variable-length key followed a sets of duplicates that have not yet been
 *    moved into their own tree (a WT_ITEM_KEY/KEY_OVFL item followed by two
 *    or more WT_ITEM_DUP/DUP_OVFL items).
 *
 * WT_PAGE_DUP_INT (row-store offpage duplicates internal pages):
 * Variable-length key and offpage-reference pairs (a WT_ITEM_KEY/KEY_OVFL item
 * followed by a WT_ITEM_OFF_INT/OFF_LEAF item).
 *
 * WT_PAGE_DUP_LEAF (row-store offpage duplicates leaf pages):
 * Variable-length data items (WT_ITEM_DUP/DUP_OVFL).
 *
 * WT_PAGE_COL_VAR (Column-store leaf page storing variable-length items):
 * Variable-length data items (WT_ITEM_DATA/DATA_OVFL).
 *
 * WT_PAGE_COL_INT (Column-store internal page):
 * WT_PAGE_COL_FIX (Column-store leaf page storing fixed-length items):
 * WT_PAGE_OVFL (Overflow page):
 *	These pages contain fixed-sized structures (WT_PAGE_COL_INT and
 *	WT_PAGE_COL_FIX), or a string of bytes (WT_PAGE_OVFL), and so do
 *	not contain WT_ITEM structures.
 */
#define	WT_ITEM_KEY		0x01000000 /* Key */
#define	WT_ITEM_KEY_OVFL	0x02000000 /* Overflow key */
#define	WT_ITEM_DATA		0x03000000 /* Data */
#define	WT_ITEM_DATA_OVFL	0x04000000 /* Overflow data */
#define	WT_ITEM_DUP		0x05000000 /* Duplicate data */
#define	WT_ITEM_DUP_OVFL	0x06000000 /* Overflow duplicate data */
#define	WT_ITEM_OFF_INT		0x07000000 /* Offpage-tree reference */
#define	WT_ITEM_OFF_LEAF	0x08000000 /* Offpage-leaf reference */

#define	WT_ITEM_LEN(addr)						\
	(((WT_ITEM *)(addr))->__item_chunk & 0x00ffffff)
#define	WT_ITEM_LEN_SET(addr, len)					\
	(((WT_ITEM *)(addr))->__item_chunk = WT_ITEM_TYPE(addr) | (len))
#define	WT_ITEM_TYPE(addr)						\
	(((WT_ITEM *)(addr))->__item_chunk & 0x0f000000)
#define	WT_ITEM_TYPE_SET(addr, type)					\
	(((WT_ITEM *)(addr))->__item_chunk = WT_ITEM_LEN(addr) | (type))

/* WT_ITEM_BYTE is the first data byte for an item. */
#define	WT_ITEM_BYTE(addr)						\
	((u_int8_t *)(addr) + sizeof(WT_ITEM))

/*
 * On row-store pages, the on-page data referenced by the WT_INDX page_data
 * field may be a WT_OVFL (which contains the address for the start of the
 * overflow pages and its length), or a WT_OFF structure.  These macros do
 * the cast for the right type, and, in the case of WT_OVFL, reach into the
 * on-page structure and return the values.
 */
#define	WT_ITEM_BYTE_OFF(addr)						\
	((WT_OFF *)(WT_ITEM_BYTE(addr)))

#define	WT_ITEM_BYTE_OVFL(addr)						\
	((WT_OVFL *)(WT_ITEM_BYTE(addr)))
#define	WT_ITEM_OVFL_ADDR(ip)						\
	(WT_ITEM_BYTE_OVFL((ip)->page_data)->addr)
#define	WT_ITEM_OVFL_LEN(ip)						\
	(WT_ITEM_BYTE_OVFL((ip)->page_data)->len)

/*
 * The number of bytes required to store a WT_ITEM followed by len additional
 * bytes.  Align the entry and the data itself to a 4-byte boundary so it's
 * possible to directly access WT_ITEMs on the page.
 */
#define	WT_ITEM_SPACE_REQ(len)						\
	WT_ALIGN(sizeof(WT_ITEM) + (len), sizeof(u_int32_t))

/* WT_ITEM_NEXT is the first byte of the next item. */
#define	WT_ITEM_NEXT(item)						\
	((WT_ITEM *)((u_int8_t *)(item) + WT_ITEM_SPACE_REQ(WT_ITEM_LEN(item))))

/* WT_ITEM_FOREACH is a loop that walks the items on a page */
#define	WT_ITEM_FOREACH(page, item, i)					\
	for ((item) = (WT_ITEM *)WT_PAGE_BYTE(page),			\
	    (i) = (page)->hdr->u.entries;				\
	    (i) > 0; (item) = WT_ITEM_NEXT(item), --(i))

/*
 * Btree internal items and offpage duplicates reference another page.
 *
 * !!!
 * We need a page's size before reading it, and as internal and leaf nodes are
 * potentially of different sizes, we have to know the page's tree level to know
 * its size.  We could store the level or type of the root of the offpage tree
 * in the WT_OFF structure, but that costs another 4 bytes (with alignment).
 *
 * On pages with variable-length items, we encode the information in the item
 * type: WT_ITEM_OFFP_INTL is an internal page reference, and WT_ITEM_OFFP_LEAF
 * is a leaf page reference.
 *
 * On pages without variable-length items (column-store internal pages), we
 * encode the information in the page header's flags field.
 *
 * This complicates the code, but 4 bytes per offpage reference is a real cost
 * we don't want to pay.
 */
struct __wt_off {
/*
 * Solaris and the gcc compiler on Linux pad the WT_OFF structure because of the
 * 64-bit records field.   This is an on-disk structure, which means we have to
 * have a fixed size, without padding, so we declare it as two 32-bit fields and
 * cast it.  We haven't yet found a compiler that aligns the 32-bit fields such
 * that a cast won't work; if we find one, we'll have to go to bit masks, or to
 * reading/write the bytes to/from a local variable.
 */
#define	WT_RECORDS(offp)	(*(u_int64_t *)(&(offp)->__record_chunk[0]))
	u_int32_t __record_chunk[2];	/* Subtree record count */
	u_int32_t addr;			/* Subtree address */
};
/*
 * WT_OFF_SIZE is the expected structure size --  we check at startup to
 * ensure the compiler hasn't inserted padding (which would break the world).
 */
#define	WT_OFF_SIZE	12

/* WT_OFF_FOREACH is a loop that walks offpage references on a page */
#define	WT_OFF_FOREACH(page, offp, i)					\
	for ((offp) = (WT_OFF *)WT_PAGE_BYTE(page),			\
	    (i) = (page)->hdr->u.entries; (i) > 0; ++(offp), --(i))

/*
 * Btree overflow items reference another page, and so the data is another
 * structure.
 */
struct __wt_ovfl {
	u_int32_t len;			/* Overflow length */
	u_int32_t addr;			/* Overflow address */
};
/*
 * WT_OVFL_SIZE is the expected structure size --  we check at startup to
 * ensure the compiler hasn't inserted padding (which would break the world).
 */
#define	WT_OVFL_SIZE	8

/* WT_FIX_FOREACH is a loop that walks fixed-length references on a page. */
#define	WT_FIX_FOREACH(db, page, p, i)					\
	for ((p) = WT_PAGE_BYTE(page),					\
	    (i) = (page)->hdr->u.entries;				\
	    (i) > 0; --(i), p = (u_int8_t *)p + (db)->fixed_len)
/*
 * WT_FIX_REPEAT_FOREACH is a loop that walks fixed-length, repeat-counted
 * references on a page.
 */
#define	WT_FIX_REPEAT_FOREACH(db, page, p, i)				\
	for ((p) = WT_PAGE_BYTE(page),					\
	   (i) = (page)->hdr->u.entries; (i) > 0;			\
	   (i) -= *(u_int16_t *)p,					\
	   p = (u_int8_t *)p + (db)->fixed_len + sizeof(u_int16_t))
/*
 * WT_FIX_REPEAT_ITERATE is a loop that walks fixed-length, repeat-counted
 * references on a page, visiting each entry the appropriate number of times.
 */
#define	WT_FIX_REPEAT_ITERATE(db, page, p, i, j)			\
	WT_FIX_REPEAT_FOREACH(db, page, p, i)				\
		for ((j) = *(u_int16_t *)p; (j) > 0; --(j))

#if defined(__cplusplus)
}
#endif
