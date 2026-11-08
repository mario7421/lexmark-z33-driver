// rastertolxx74 - Convert CUPS raster data to Lexmark X74 compatible printer data

// Copyright (C) 2004 Enrique Perez-Terron of Norway
// Copyright (C) 2005 Vitaly V. Bursov <vitalyvb@ukr.net>
// 			- Modified for Z33

// This program is free software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along with this
// program, see the file LICENSE; if not, write to the Free Software Foundation, Inc., 59
// Temple Place - Suite 330, Boston, MA 02111-1307, USA.

// Please report bugs to Enrique Perez-Terron <enrio@online.no>
//////////////////////////////////////////////////////////////////////////////////////////

// The output consists of: 
// - A preamble, 
// - page initialization,
// - a sequence of "bands",
// - page finalization..
// (repeat the last three as required)
// 

/* The following are default values that can be modified with '-s' command line switches
   to ghostscript. 

   TODO: At present there is no adjustment of these parameters depending on resolution.
   This is clearly a bug that must be fixed if any other than the default resolution will
   work.
*/

/* Add this to the x coordinates when printing right-to-left in order to line up with the
   left-to-right data. */
#define DEFAULT_RTL_ADJUST 4

/* Each band must leave the print head in a position suitable to do the next band in one
   direction only. I.e., if the next band goes right-to-left and starts at X, the current
   band must leave the print head at X + ACCELLERATION_MARGIN or more. If the next band is
   printed left-to-right, the print head must be left at X - ACCELLERATION_MARGIN or
   less. */
#define ACCELLERATION_MARGIN 450

// A band consists of a color subband, and/or a black subband.  A subband consists of a
// set of "escape commands" and an array of "ink lines". Escape commands contain statistics
// like the number of dots in each color, the horizontal offset of the first ink line, the
// total number of bytes comprising the subband's ink lines, and the vertical paper feed
// to follow the band, i.e., the distance to the next band. The ink lines contain
// information about what inkjet nozzles to activate at once at the current position of
// the print head. The ink line array is without gaps, i.e., for a given horizontal
// resolution there is one ink line for each horizontal position between the the first and
// last non-empty inkline.

// Ink nozzles are 1/600 inch appart vertically. Printing at higher resolution is possible
// by feeding the paper 1/1200 inch.  Horizontally, the print head can move as little as
// 1/2400 inch between activations of the print head. However, the default is 1/600 inch.

// NB! If the default resolution is set any higher than the minimum resolution, some code
// must be modified. Look for the lxx74->y_res and lxx74->xres multipliers and consider
// what happens if the actual resolution is lower than the default.

#define DEFAULT_X_RESOLUTION 600
#define MIN_X_RESOLUTION 600
#define MAX_X_RESOLUTION 2400
#define DEFAULT_Y_RESOLUTION 600
#define MIN_Y_RESOLUTION 600
#define MAX_Y_RESOLUTION 1200

// Suppose the print head is situated relative to the paper such that inkjet nozzle number
// zero of the color cartridge can place an ink dot at coordinates (x, y) on the
// paper. Then inkjet number 'n' can write a dot at position (x + cx(n), y + cy(n)).

//     cx(n)  =  ((n is even) ? 0 : odd_offset(n)/x_dpi) inches
//
//     cy(n)  =  (y_offset(n) + (n - base(n))/600) inches

// If 'n' is in the range CYAN_BASE .. CYAN_BASE+CYAN_NOZZLES-1, then color nozzle 'n'
// produces cyan dots. Similar relations apply to the two other cmy primary colors,
// magenta and yellow.

// y_offset and base depends on the color. There are 64 nozzles of each color. The group
// of nozzles of the same color constinute a "pen". There is a gap of 32 positions between
// the pens.
// y_offset(n) = (n>>6) * 96
// base(n)     = (n>>6) * 64 = n & ~63

// We can simplify cy(n) in a number of ways:
// (n/64)*(64+32) + (n - (n/64)*64) == n + (n/64)*32 == n + ((n>>6)<<5) 

// where the last term in the last expression simply accounts for the gaps.
// 
// The *_OFFSET constants below are in units of 1/600 inches (actually the default resolution).

#define CYAN_BASE 0
#define CYAN_NOZZLES 64
#define CYAN_Y_OFFSET 0
#define CYAN_X_OFFSET 0
#define CYAN_ODD_OFFSET 16

#define MAGENTA_BASE (CYAN_BASE + CYAN_NOZZLES)
#define MAGENTA_NOZZLES 64
#define MAGENTA_Y_OFFSET (MAGENTA_BASE + 28)
#define MAGENTA_X_OFFSET 0
#define MAGENTA_ODD_OFFSET 16

#define YELLOW_BASE (MAGENTA_BASE + MAGENTA_NOZZLES)
#define YELLOW_NOZZLES 64
#define YELLOW_Y_OFFSET (YELLOW_BASE + 56)
#define YELLOW_X_OFFSET 0
#define YELLOW_ODD_OFFSET 16

// The black ink nozzles are numbered separately, from zero.  However, for the same
// vertical position of the printhead vs paper, black inkjet nozzle number zero produces a
// black dot at position (x + bx(n), y + by(n)).

//     bx(n)  =  b_adj_x + (n is even) ? 0 : BLACK_ODD_OFFSET
//
//     by(n)  =  b_adj_y + black_y_ofset + n/600 inch

// The parameters b_adj_x, b_adj_y are normally zero, but may change slightly if either
// the color ink cartridge or the black ink cartridge are replaced. The driver probably
// needs to keep these values in a configuration file, and the user should have a means to
// tune these values after studying a special alignment printout.

// There is potentially one additional paramenter that might need tuning by the user, the
// horizontal adjustment to align dots produced in a left-to-right print head movement
// with those made in right-to-left print head movements.

#define BLACK_BASE 0
#define BLACK_NOZZLES (lxx74->mono ? 208 : 192)
#define BLACK_Y_OFFSET (lxx74->mono ? 13 : 21)
#define BLACK_X_OFFSET 876
#define BLACK_ODD_OFFSET 16

/* The black cartridge adjustments are in the highest possible resolution. */
#define B_ADJ_Y_DPI 1200
#define B_ADJ_X_DPI 2400

// This is a physical limitation of the printer:
#define MAX_WIDTH_MM 225
// There is no hard limit to the height of the paper.

///////////////////////////////////////////////////////////////////////

/* Ink line: An ink line contains one bit per inkjet nozzle in one ink cartridge.  Bits
   that are set correspond to nozzles that will fire simultaneously.  The external format
   of the ink line limits it to 192 bits (24 bytes). The external format is a compressed
   format that adds four bytes of overhead, but potentially omits some of the bytes.  In
   the worst case, we end up with 28 bytes per ink line.  We set aside memory to store
   24-bit ink lines, as many as required for a complete horizontal swipe, and only do the
   compression on the fly when writing the data to the output file or device. At that time
   we will also know if we are doing left-to-right or right-to-left printing.

   However, the number of ink lines must account for the maximal horizontal resolution
   (2400 dpi) and also account for the odd_offset. In order to print dots at the left edge
   of the printable region using odd-numbered inkjet nozzles, the nominal printhead x
   coordinate must be -xxx_odd_offset. At the other end of the printable region, the x
   coordinate must be the full width of the printable region when using the even-numbered
   inkjet nozzles.

   Reading other Lexmark drivers, I realize that the format can allow more than 192
   nozzles.  The external ink line format starts with a byte that indicates which
   compression method is used, zero-suppression (0x80) or run-length (0x40). Then
   follows the 24 bits (tree bytes) of directory, where each bit signals the presence
   or suppression of a byte of ink data. However, using the lower bits of the compression
   indicator byte, more than 24 bytes of data can be compressed.  I might discover
   a way of using any further nozzles if they are present on the ink cartridge.

*/

#include <errno.h>
#define debug(format, ...)  fprintf(stderr, "DEBUG: %s:%d: " format ".\n", __FILE__, __LINE__, ## __VA_ARGS__)
#define error(format, ...)  fprintf(stderr, "ERROR: %s:%d: " format ".\n", __FILE__, __LINE__, ## __VA_ARGS__)

#include <stdio.h>
typedef unsigned char byte;
#define LXX74_EOF (-777)
#define gs_error_Fatal (-100)
#define gs_error_VMerror (-25)
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <setjmp.h>
jmp_buf errorjmp;


#define INKLINE_BYTES (lxx74->mono ? 26 : 24)
typedef byte inkline[26];

#define COLOR 1
#define CYAN 3
#define MAGENTA 2
#define YELLOW 1
#define BLACK 0
#define NUM_PENS 4
#define MASK(color) (1 << color)

// The band structure is really a subband. The function makebands creates one color and
// one black subband and fill in the data.  If it turns out that one of them is empty,
// only the other is queued for printing.  The bands are not printed until the next band
// is created, since we need to know the vertical distance to the next band before we can
// print the currend band.

typedef struct band_s 
{
    struct band_s *next, *prev;	/* prev is printed before this */
    int y;			/* y value of nozzle zero */
    int x1;			/* first (leftmost) x-value (inkline, even nozzle
				   numbers) */
    int x2;			/* last x-value */
    int type; 			/* 0 = color, 3 = black */
    int dots[NUM_PENS];		/* count dots of each color, all inklines */
    int inkline_bytes;		/* bytes of compressed data */
    inkline *ink;		/* pointer to array of x2-x1+1 inklines */
} band_t;

// Ghostscript offers an opportunity to avoid copying scanlines.  Since nothing is
// documented as to when this will happen, the program must either always copy the data or
// keep track of which scanlines were copied and which were not.  Scanlines that were
// copied are owned by us and must be freed, other scanlines must not be freed.  For this
// we use a bitmap.  We keep our bitmap in an array of bytes.

#define bitmap_set(map, n)    ((map)[(n) >> 3] |= 1 << ((n) & 7))
#define bitmap_clear(map, n)  ((map)[(n) >> 3] &= ~(1 << ((n) & 7)))
#define bitmap_test(map, n)   (1 & ((map)[(n) >> 3] >> ((n) & 7)))

// The pixbuf is like a window into the page, consisting of 'height' scanlines.  The pens
// operate on the lines in the pixbuf. Shifting the lines up in the pix->scan arrray is
// equivalent to feeding the paper.

typedef struct pixbuf_s
{
    int y;			/* vertical page coordinate of scan[0] */
    int height;			/* current number of valid pointers in scan[] */
    int maxheight;		/* capacity of scan[] */
    byte *own;			/* pointer to bitmap; bitmap_test(own, n) is set if
				   scan[n] is owned by us i.e., we must do
				   lxx74_free(scan[n]). */
    byte **scan;		/* pointer to array of pointers to scanlines */
} pixbuf_t;

// Functions that operate on the pixbuf:
typedef struct lxx74_device_s lxx74_device_t;
static void initialize_pens_and_pixbuf(lxx74_device_t *lxx74); /* allocate and initialize */
static int get_scanlines(lxx74_device_t *lxx74); /* Retrieve scanlines from ghostscript,
						     add pointers to pix->scan array */
static int feed_scanlines(lxx74_device_t *lxx74); /* Remove empty scanlines from top of
						     pixbuf, move remaining lines up, and
						     call get_scanlines() to fill up
						     below. Models a paper feed. */
static void make_bands(lxx74_device_t * lxx74); /* Transfer color components from the
						   pixbuf to inklines in bands. Zero out
						   color component in pixbuf. Enqueue the
						   bands. */
static void finalize_pixbuf(lxx74_device_t *lxx74); /* Free memory */

// A pen is a set of inkjet nozzles on an ink cartridge, all emitting ink of the same
// color. When the print head moves or the paper is fed, all pens move together and
// maintain their relative positions.  The pen's vertical positions are fixed with respect
// to the pixbuffer. A pen can also be seen as a region of the pixbuffer. Notice that if
// the vertical resolution is higher than the distance beween ink nozzles, there are
// unreachable scanlines between those corresponding to the pen nozzles. These become
// reachable when the paper is fed forward, which corresponds to shifting the scanlines up
// the pixbuffer.scan array.
typedef struct pen_s
{
    int color;			/* see the defines below, CYAN..BLACK */
    int mask;			/* MASK(color) used to extract a color component from a pixel */
    int base;			/* Ordinal number of the first nozzle in cartridge belonging */
    int nozzles;		/* Cardinal number of nozzles belonging to this pen */
    int y_offset;		/* Ordinal number of pixbuffer scanline corresponding to
				   nozzle number 'base' */
    int x_offset;		/* Zero for color pens */
    int odd_offset;		/* Number of pixels between even and odd-numbered nozzles,
				   using the current print resolution. */
    int max_offset;		/* The Ordinal number in the pixbuf of the scanline corres-
				   ponding to (the fictitious) nozzle number 'base + nozzle' */
} pen_t;

// Ghostscript requires a device structure. Attributes specific to the X74 can be added at
// the end.

/* The device structure. First, the procedures. */

static int lxx74_get_bits(lxx74_device_t *lxx74, int y, byte *buffer, byte **bufptr);

/* The device structure type */

struct lxx74_device_s 
{
    int mono;
    int width;
    int height;
    int bytesperline;
    float HWResolution[2];
    /* Adjustable Parameters */
    int black_adj_x;		/* Adjustment of the black inks */
    int black_adj_y;		/*   wrt the color inks. Default zero. */
    int rtl_adj;		/* Bi-directional adjustment of the x
				   position. Default zero */
    int accel;			/* space to move the print head before it comes up to
				   speed */

    int initialized;
    

    /* Auxiliary variables */
    int y_res;			/* vertical resolution in units of the printhead
				   internozzle distance , ydpi / MIN_Y_RESOLUTION */
    int x_res;			/* xdpi / MIN_X_RESOLUTION */

    int x_min;			/* x printhead coordinate when rightmost nozzles are at
				   x=0.  Equals minus the maximum odd_offset over all
				   pens */
    int x_max;			/* x printhead coordinate when leftmost nozzles are at x=width-1 */
    int x;			/* keep track of the ending x position of the printhead */
    int direction;		/* after last band printed */
    
    int top_pen;
    int bottom_pen;		/* The pen whose vertical offset is largest */
    int bands_printed;

    pen_t pens[NUM_PENS];
    pixbuf_t *pix;
    band_t *bands_head;
    band_t *bands_tail;
    band_t *bands_free;
    byte *scanbuf;

    int preamble_done;
};


static void *lxx74_malloc(size_t size);
static void inline lxx74_free(void *mem);

#include <cups/cups.h>
#include <cups/raster.h>

/* ------------------------------------------------------------------
 * Port to modern CUPS (2.x):
 * Reads CUPS raster v1 and v2 page headers with plain fread() on
 * stdin, so the pixel reads in lxx74_get_bits() stay consistent on
 * the same stdio stream (the 2004 code did this too, but only
 * understood the v1 sync word; libcupsimage's read()-based buffering
 * cannot be mixed with our fread() pixel reads).
 * v1 and v2 headers share the offsets of all fields we use; v2 just
 * has 1376 extra bytes appended after byte 420.
 * ------------------------------------------------------------------ */
#define CUPS_RASTER_SYNC_V2 0x52615332 /* 'RaS2' */

static int read_cups_header(FILE *f, cups_page_header2_t *h)
{
    unsigned char *buf = (unsigned char *)h;
    size_t hdrlen = 0;		/* total bytes: sync + header */
    size_t bodyoff = 0;		/* where the struct starts in buf/file */
    unsigned sync;
    int swapped = 0;

    memset(h, 0, sizeof(*h));
    if (fread(buf, 1, 4, f) != 4)
	return 0; /* clean EOF */

    /* Parse the sync word as big-endian byte sequence. NOTE: the CUPS_*
	 * constants are meant to be compared against a native-endian read,
	 * so under this big-endian parse a *normal* little-endian-host
	 * stream shows up as the REVSYNC* constant and vice versa. */
    sync = ((unsigned)buf[0] << 24) | ((unsigned)buf[1] << 16) |
	   ((unsigned)buf[2] << 8) | (unsigned)buf[3];

    if (sync == CUPS_RASTER_REVSYNCv1) {	/* bytes "tSaR": normal LE v1 */
	hdrlen = 420; bodyoff = 0;
    } else if (sync == CUPS_RASTER_SYNCv1) {	/* bytes "RaSt": BE-host v1 */
	hdrlen = 420; bodyoff = 0; swapped = 1;
    } else if (sync == CUPS_RASTER_REVSYNCv2) {	/* bytes "2SaR": normal LE v2 */
	hdrlen = sizeof(cups_page_header2_t); bodyoff = 0;
    } else if (sync == CUPS_RASTER_SYNCv2) {	/* bytes "RaS2": BE-host v2 */
	hdrlen = sizeof(cups_page_header2_t); bodyoff = 0; swapped = 1;
    } else if (sync == CUPS_RASTER_REVSYNC) {	/* bytes "3SaR": normal LE v3,
						 * modern default; sync is OUTSIDE
						 * the struct, 1800 bytes total */
	hdrlen = 4 + sizeof(cups_page_header2_t); bodyoff = 4;
    } else if (sync == CUPS_RASTER_SYNC) {	/* bytes "RaS3": BE-host v3 */
	hdrlen = 4 + sizeof(cups_page_header2_t); bodyoff = 4; swapped = 1;
    } else {
	error("Unknown CUPS raster sync word 0x%08x", sync);
	return -1;
    }

    /* v1/v2: sync overlaps MediaClass, struct remainder lands at buf+4.
     * v3: sync is separate, struct lands at buf+0. */
    if (fread(buf + (bodyoff ? 0 : 4), 1, hdrlen - 4, f) != hdrlen - 4) {
	error("Truncated CUPS raster page header");
	return -1;
    }

    if (swapped) {
	unsigned i, *w;
	for (i = 0, w = (unsigned *)(buf + 256); i < (hdrlen - bodyoff - 256)/4; i++, w++)
	    *w = ((*w >> 24) & 0xff) | ((*w >> 8) & 0xff00) |
		 ((*w << 8) & 0xff0000) | ((*w << 24) & 0xff000000);
    }
    return 1;
}

/* lxxx74->pix is initialized from lxx74_print_page() with empty scanlines corresponding to
   the cyan and magenta regions of the print head, and then pix->y and pix->height are set
   up so the the next scanline will have y coordinate zero. At this point the next
   scanline will correspond to the first yellow nozzle. Yellow is selected because
   pens[YELLOW].offset is larger than the offset of any other color. */

static void initialize_pens_and_pixbuf(lxx74_device_t *lxx74)
{
    int c, top;
    pen_t *pens = lxx74->pens;
    pixbuf_t *pix = lxx74_malloc(sizeof(*pix));

    bzero(pens, sizeof(lxx74->pens));

    lxx74->pix = pix;
    bzero(pix, sizeof(*pix));

    lxx74->y_res = (int) (lxx74->HWResolution[1] / MIN_Y_RESOLUTION + 0.5);
    lxx74->x_res = (int) (lxx74->HWResolution[0] / MIN_X_RESOLUTION + 0.5);

    for (c=0; c<NUM_PENS; ++c) {
	pens[c].color = c;
	pens[c].mask = MASK(c);
    }

    pens[CYAN].base = CYAN_BASE;
    pens[CYAN].nozzles = CYAN_NOZZLES;
    pens[CYAN].y_offset = CYAN_Y_OFFSET * lxx74->y_res;
    pens[CYAN].max_offset = (CYAN_Y_OFFSET + CYAN_NOZZLES - 1) * lxx74->y_res;
    pens[CYAN].x_offset = CYAN_X_OFFSET * lxx74->x_res;
    pens[CYAN].odd_offset = CYAN_ODD_OFFSET * lxx74->x_res;
    
    pens[MAGENTA].base = MAGENTA_BASE;
    pens[MAGENTA].nozzles = MAGENTA_NOZZLES;
    pens[MAGENTA].y_offset = MAGENTA_Y_OFFSET * lxx74->y_res;
    pens[MAGENTA].max_offset = (MAGENTA_Y_OFFSET + MAGENTA_NOZZLES - 1) * lxx74->y_res;
    pens[MAGENTA].x_offset = MAGENTA_X_OFFSET * lxx74->x_res;
    pens[MAGENTA].odd_offset = MAGENTA_ODD_OFFSET * lxx74->x_res;
    
    pens[YELLOW].base = YELLOW_BASE;
    pens[YELLOW].nozzles = YELLOW_NOZZLES;
    pens[YELLOW].y_offset = YELLOW_Y_OFFSET * lxx74->y_res;
    pens[YELLOW].max_offset = (YELLOW_Y_OFFSET + YELLOW_NOZZLES - 1) * lxx74->y_res;
    pens[YELLOW].x_offset = YELLOW_X_OFFSET * lxx74->x_res;
    pens[YELLOW].odd_offset = YELLOW_ODD_OFFSET * lxx74->x_res;
    
    pens[BLACK].base = BLACK_BASE;
    pens[BLACK].nozzles = BLACK_NOZZLES;
    pens[BLACK].y_offset = (lxx74->black_adj_y * lxx74->HWResolution[1])/MAX_Y_RESOLUTION 
	+ BLACK_Y_OFFSET * lxx74->y_res;
    pens[BLACK].max_offset = (BLACK_Y_OFFSET + BLACK_NOZZLES - 1) * lxx74->y_res;
    pens[BLACK].x_offset = (lxx74->black_adj_x * lxx74->HWResolution[0])/MAX_X_RESOLUTION 
	+ BLACK_X_OFFSET * lxx74->x_res;
    pens[BLACK].odd_offset = BLACK_ODD_OFFSET * lxx74->x_res;

    // NB! the base and nozzles attributes are pure counts (numbers).  When using them to
    // compute distances, multiply by the pixels per inter-nozzle distance.  This distance
    // is kept in lxx74->y_res.

    // NB! The various offset attributes below are in pixels, and need no correction.
    // Above, the preprocessor constants are given in pixels assuming the default or
    // minimum distance. (Here is where things must be rewritten if the default is changed.

    pix->maxheight = pens[0].y_offset + pens[0].nozzles * lxx74->y_res;

    top = pix->height = pens[0].y_offset;

    lxx74->top_pen = lxx74->bottom_pen = 0;

    lxx74->x_min = - pens[0].odd_offset;
    lxx74->x_max = pens[0].x_offset;

    for (c=1; c<NUM_PENS; ++c) {

	int height = pens[c].y_offset + pens[c].nozzles * lxx74->y_res;

	if (height > pix->maxheight)
	    pix->maxheight = height;

	if (pens[c].y_offset > pix->height) {
	    pix->height = pens[c].y_offset;
	    lxx74->bottom_pen = c;
	}

	if (pens[c].y_offset < top) {
	    top = pens[c].y_offset;
	    lxx74->top_pen = c;
	}

	if (pens[c].odd_offset > - lxx74->x_min)
	    lxx74->x_min = - pens[c].odd_offset;

	if (pens[c].x_offset > lxx74->x_max)
	    lxx74->x_max = pens[c].x_offset;
    }

    lxx74->x_max += lxx74->width;
    
    pix->y = - pix->height;

    pix->own = lxx74_malloc((pix->maxheight + 7) / 8);
    bzero(pix->own, (pix->maxheight + 7) / 8);

    /* nil pointers count as empty scan lines. */
    pix->scan = lxx74_malloc(pix->maxheight * sizeof(byte*));
    bzero(pix->scan, pix->maxheight * sizeof(byte*));
}

static void finalize_pixbuf(lxx74_device_t *lxx74)
{
    pixbuf_t *pix = lxx74->pix;

    if (pix) {
	byte **scan = pix->scan;
	byte *own = pix->own;;
    
	if (own && scan) {
	    int n;
	    for (n=0; n<pix->maxheight; ++n)
		if (scan[n] && bitmap_test(own, n))
		    lxx74_free(scan[n]);
	}
	
	if (own)
	    lxx74_free(own);

	if (scan)
	    lxx74_free(scan);

	lxx74_free(pix);
	lxx74->pix = 0;
    }
}

/* Shift bits n positions toward lower bit addresses. */
/* The evacuated positions are zeroed. */
/* Map has maxbit bits, numbered 0 .. (maxbit-1). */
static void bitmap_shift_down(byte *map, int n, int maxbit)
{
    byte *source, *dest, *last_source, *last_dest;
    long buffer; /* large enough to hold two bytes */

    if (n < 0)
	n = 0;
    
    if (n > maxbit)
	n = maxbit;

    /* The bits to keep are in these bytes */
    source      = & map[n >> 3];
    last_source = & map[(maxbit - 1) >> 3];

    /* Substract n from each index above */
    dest        = & map[0];
    last_dest   = & map[(maxbit - n - 1) >> 3];

    /* If n == maxbit we could need this test. */
    if (source <= last_source)
	buffer = *source++;
    else
	buffer = 0;

    /* we only need the lower bits of n from now on */
    n &= 7;
    
    while (dest <= last_dest) {

	if (source <= last_source)
	    buffer = (buffer << 8) | *source++;
	else
	    buffer = buffer << 8;

	*dest++ = 0xff & (buffer >> n);
    }

    while (dest <= last_source)
	*dest++ = 0;    
}
	
/* g e t _ s c a n l i n e s */
/* ------------------------- */

/* Fill up lxx74->pix with scanlines from ghostscript */
/* At end of page, fill up with empty scanlines. */
/* Return zero if all scanlines are empty */
static int get_scanlines(lxx74_device_t *lxx74)
{
    int retcode = 0;
    int code = 0;
    pixbuf_t *pix = lxx74->pix;
    byte **scan = pix->scan;
    byte *own = pix->own;

    while (pix->height < pix->maxheight) {
	int x;
	int y = pix->y + pix->height; /* scanline y coordinate */
	int n = pix->height;	      /* current pixbuf scanline */

	if (y < lxx74->height) {      /* within the end of the page */

	    if (bitmap_test(own, n)) {
		/* Should never happen. Remove old scanline from earlier round */
		if (scan[n]) {
		    if (!lxx74->scanbuf)
			lxx74->scanbuf = scan[n]; /* Keep one buffer handy */
		    else
			lxx74_free(scan[n]);
		}
		bitmap_clear(own, n);
	    }
	    scan[n] = 0;

	    if (! lxx74->scanbuf) /* Make sure we have a buffer */
		lxx74->scanbuf = lxx74_malloc(lxx74->bytesperline);

	    /* Get a scanline from ghostscript */

	    code = lxx74_get_bits(lxx74, y, lxx74->scanbuf, &scan[n]);
	    if (code < 0)
		longjmp(errorjmp, code);
	    code = 0;

	    /* If the scanline is all zeros, use a null pointer instead. */
	    for (x=0; x<lxx74->bytesperline; ++x)
		if (scan[n][x] != 0)
		    break;
	    if (x == lxx74->bytesperline)
		scan[n] = 0;
	    else {
		bitmap_set(own, n);
		lxx74->scanbuf = 0;
		retcode = 1;

		/* When using the 'bitcmyk' driver, ghostscript sometimes produce black by
		   combining CMY. Turn those pixels back to black */

		while (x < lxx74->bytesperline) {
		    int twopixels = scan[n][x];
		    int badblack1 = (MASK(CYAN) | MASK(MAGENTA) | MASK(YELLOW));
		    int badblack2 = badblack1 << 4;
		    int bool1 = (((twopixels & badblack2) == badblack2)
				 && (twopixels = ((MASK(BLACK) << 4) | (twopixels & 15))));
		    int bool2 = (((twopixels & badblack1) == badblack1)
				 && (twopixels = ((twopixels & 0xf0) | MASK(BLACK))));

		    if (bool1 || bool2) scan[n][x] = twopixels;

		    ++x;
		}
	    }
	}
	else {
	    /* Beyond the end of the page, nil pointers count as empty scanlines. */
	    if (scan[n]) {
		if (bitmap_test(own, n)) {
		    bitmap_clear(own, n); /* Should never happen */
		    lxx74_free(scan[n]);
		}
		scan[n] = 0;
	    }
	}

	++pix->height;
    }

    return retcode;
}

/* The first line in the cyan area is scanned for cyan color contents, the first line
   in the magenta area is scanned for magenta contents, and so on for yellow and black
   too. If all first lines are empty of their respective colors, we advance the print head
   by one scanline, discarding one scanline at the top of the pixbuf, and adding one more
   line at the bottom getting it from ghostscript or making it an empty line if the end of
   the page has been reached.

   This process is repeated until the first scanline in a color region has pixels with the
   corresponding color, or until the printhead position is beyond the end of the page. At
   this point feed_scanlines() returns zero in the first case, 1 in the second
   (end-of-page) case.

   */

static int feed_scanlines(lxx74_device_t *lxx74)
{
    pixbuf_t *pix = lxx74->pix;
    byte **scan = pix->scan;
    byte *own = pix->own;
    pen_t *pens = lxx74->pens;

    while (1) {
	/* Look for pixels of the corresponding color in the first scanline of each pen */

	/* For efficiency, we do not shift up the scanlines for each line discarded from
	   the top. Instead we keep track of the number of lines to discard in 'dn'. */

	int dn;

	int pixel = 0;

	// The loop below should stop when any one of the pens are beyond the end of valid
	// data in the pixbuf, so we need to gather more data from gs before continuing.
	// We must also stop if we have pixels of the appropriate color in the first row
	// of any pen, of course.  Both criteria are tested in the inner loop, so we use a
	// goto to break out of both loops.

	for (dn = 0; 1; ++dn) {
	    int c;
	    
	    for (c=0; c<NUM_PENS; ++c) {
		byte *line;
		int n = pens[c].y_offset + dn;

		if (n >= pix->height)
		    goto discard_scanlines;

		// y_offset must be non-negative so that we have n >= dn.
		// Then we also have dn <= n < pix_height

		line = scan[n];
		if (line) {	/* Null pointers count as empty scanlines */
		    int x2, x2_max; /* x2 means x/2. */
		    int mask = pens[c].mask;

		    // In the following code we have hardcoded the fact that our depth is
		    // four. That means each byte of the scanline contains exactly two
		    // pixels. The order is bit-bigendian, i.e., (byte >> 4) is the first
		    // of the two

		    // Test both pixels at once
		    mask = (mask << 4) + (mask & 15);
		    x2_max = lxx74->width >> 1;

		    if (lxx74->width & 1)      // In case there is an odd number of pixels, and
			line[x2_max++] &= ~(15<<4); // gs leaves cruft in the last nibble...
		    
		    for (x2=0; x2 < x2_max; ++x2) {
			pixel = line[x2];
			if (pixel &= mask)     // Leave result in 'pixel' for later...
			    {
				n = 0; // Dummy, so we can set a breakpoint
				
				goto discard_scanlines;
			    }
		    }
		}
	    }
	}

    discard_scanlines:
	if (dn) {
	    /* The first dn scanlines of each pen region contains no pixel with the relevant
	       color.  Move the print head down (or feed the paper up) by dn steps.
	       Discard the top dn scanlines from the pixbuf and get new scanlines from
	       ghostscript. */

	    int n;
	    
	    for (n = 0; n < dn; ++n)
		if (scan[n] && bitmap_test(own, n)) {
		    bitmap_clear(own, n);
		    lxx74_free(scan[n]);
		    scan[n] = 0;
		}
	    
	    pix->y += dn;
	    pix->height -= dn;
	    if (pix->height) {
		bitmap_shift_down(own, dn, pix->height);
		memmove(scan, &scan[dn], pix->height * sizeof(byte*));
	    }
	    bzero(&scan[pix->height], dn * sizeof(byte*));

	    // Since we had dn <= pix->height (equal because dn can increment once after
	    // the last time the dn < pix->height invariant was assured), we now have
	    // pix->height >= 0.
	}

	/* Fill up the pixbuf with scanlines from ghostscript */
	if (get_scanlines(lxx74)) {
	    dn = 0; 		/* Dummy, to set breakpoint */
	}
	
	
	if (pixel)
	    return 0;

	if (pix->y >= lxx74->height)
	    return 1;
    }
}

static band_t *get_band(lxx74_device_t * lxx74)
{
    band_t *band = lxx74->bands_free;
    if (band) {
	lxx74->bands_free = band->next;
	band->next = 0;
    }
    else {
	band = lxx74_malloc(sizeof(band_t));
	bzero(band, sizeof(band_t));
	band->ink = lxx74_malloc((lxx74->x_max - lxx74->x_min)*sizeof(inkline));
	/* We will index into band->ink with x = lxx74->x_min .. lxx74->x_max - 1 */
	band->ink -= lxx74->x_min;
    }
    return band;
}

/* Only enqueue the band if it is non-empty, otherwise add it to the free list. */
static void enqueue_band(lxx74_device_t *lxx74, band_t *band)
{
    if (band->x2 >= lxx74->x_min) {
	if (lxx74->bands_tail) {
	    band->prev = lxx74->bands_tail;
	    lxx74->bands_tail->next = band;
	}
	else
	    lxx74->bands_head = band;
	lxx74->bands_tail = band;
    }
    else {
	band->next = lxx74->bands_free;
	lxx74->bands_free = band;
    }
}

static void finalize_band(lxx74_device_t *lxx74, band_t *band)
{
    if (band) {
	if (band->ink) {
	    band->ink += lxx74->x_min;
	    lxx74_free(band->ink);
	}
	
	lxx74_free(band);
    }
}

static void make_bands(lxx74_device_t *lxx74)
{
    int x;
    
    band_t *colorband = get_band(lxx74);
    band_t *blackband = get_band(lxx74);
    band_t *band;
    pixbuf_t *pix = lxx74->pix;
    byte **scan = pix->scan;
    pen_t *pen;
    
    colorband->type = COLOR;
    blackband->type = BLACK;
    colorband->y = blackband->y = lxx74->pix->y;
    colorband->x1 = blackband->x1 = lxx74->x_max;
    colorband->x2 = blackband->x2 = lxx74->x_min - 1;
    
    bzero(&colorband->ink[lxx74->x_min], (lxx74->x_max - lxx74->x_min)*sizeof(inkline));
    bzero(&blackband->ink[lxx74->x_min], (lxx74->x_max - lxx74->x_min)*sizeof(inkline));
    bzero(colorband->dots, NUM_PENS*sizeof(int));
    bzero(blackband->dots, NUM_PENS*sizeof(int));

    for (x = 0; x < lxx74->width; ++x) {
	for (pen = lxx74->pens; pen < lxx74->pens + NUM_PENS; ++pen) {
	    int nozzle;
	    band = pen->color == BLACK ? blackband : colorband;
	    for (nozzle = pen->base; nozzle < pen->base + pen->nozzles; ++nozzle) {
		int   n       = pen->y_offset + (nozzle - pen->base)*lxx74->y_res;
		if (scan[n]) {
		    byte *pixbyte = &scan[n][x>>1]; /* Hardcoding two pixels per byte */
		    int   ink_x   = x - (nozzle&1 ? pen->odd_offset : 0) + pen->x_offset;
		    byte *inkbyte = &band->ink[ink_x][nozzle>>3];
		    int   mask    = pen->mask;		
		    if ((x&1) == 0)
			mask <<= 4;
		    if (*pixbyte & mask) {
			*pixbyte &= ~mask; /* Clear color component from pixbuf */

			if (*inkbyte & (1 << (7 - (nozzle&7)))) // bit-bigendian!
			    debug("Setting ink bit twice (or more)!! x = %d, y = %d", x, band->y + n);
			else {
			    *inkbyte |= 1 << (7 - (nozzle&7)); /* Add dot to inkline */
			    ++band->dots[pen->color];
			}
		    
			if (ink_x < band->x1)
			    band->x1 = ink_x;
			if (ink_x > band->x2)
			    band->x2 = ink_x;
		    }
		}
	    }
	}
    }
    
    enqueue_band(lxx74, colorband);
    enqueue_band(lxx74, blackband);
}

static void cmd16(byte *cmdp, int i16)
{
    *cmdp++ = i16 >> 8;
    *cmdp = i16;
}
static void cmd24(byte *cmdp, int i24)
{
    *cmdp++ = i24 >> 16;
    *cmdp++ = i24 >> 8;
    *cmdp = i24;
}
static void cmd32(byte *cmdp, int i32)
{
    *cmdp++ = i32 >> 24;
    *cmdp++ = i32 >> 16;
    *cmdp++ = i32 >> 8;
    *cmdp = i32;
}

static void checksum(byte *cmdp)
{
    int s = 0;
    int n;
    for (n=1; n<7; ++n)
	s += cmdp[n];
    cmdp[7] = s;
}

static int bytes_inklines(lxx74_device_t *lxx74, band_t *band)
{
    int num_bytes = 0;
    int x;
    
    for (x = band->x1; x <= band->x2; ++x){
	int n;
	int rle_count = 0, zse_count = 0, prevbyte = -1;
	inkline *ink = & band->ink[x];
	for (n = 0; n<INKLINE_BYTES; ++n){
	    byte b = ink[0][n];
	    if (b)
		++zse_count;
	    if (b != prevbyte) {
		prevbyte = b;
		++rle_count;
	    }
	}
	num_bytes += rle_count < zse_count ? rle_count : zse_count;
    }
    return num_bytes + 4 * (band->x2 + 1 - band->x1);
}

static void write_inklines(lxx74_device_t *lxx74, band_t *band, int left_to_right, FILE *f)
{
    int begin, end, i, step;
    
    if (left_to_right) {
	begin = band->x1;
	end = band->x2 + 1;
	step = 1;
    }
    else {
	begin = band->x2;
	end = band->x1 - 1;
	step = -1;
    }
    
    for (i = begin; i != end; i += step){
	int n;
	int prevbyte = -1;
	int rle_count = 0, zse_count = 0;
	int rle_dir = 0, zse_dir = 0;
	
	inkline *ink = &band->ink[i];
	
	for (n=0; n<INKLINE_BYTES; ++n) {
	    byte b = ink[0][n];
	    
	    if (b) {
		++zse_count;
		zse_dir |= 1 << ((INKLINE_BYTES - 1) - n);
	    }
	    if (b != prevbyte) {
		prevbyte = b;
		++rle_count;
		rle_dir |= 1 << ((INKLINE_BYTES - 1) - n);
	    }
	}
	if (rle_count < zse_count) {
	    byte cmd[4];
	    cmd[0]=0x40 | ((~rle_dir >> 24) & 0x03);
	    cmd24(cmd+1, ~rle_dir);
	    fwrite(cmd, 1, 4, f);
	    prevbyte = -1;
	    
	    for (n=0; n<INKLINE_BYTES; ++n) {
		byte b = ink[0][n];
		if (b != prevbyte) {
		    prevbyte = b;
		    fputc(b, f);
		}
	    }
	}
	else {
	    byte cmd[4];
	    cmd[0]=0x80 | ((~zse_dir >> 24) & 0x03);
	    cmd24(cmd+1, ~zse_dir);
	    fwrite(cmd, 1, 4, f);
	    
	    for (n=0; n<INKLINE_BYTES; ++n) {
		byte b = ink[0][n];
		if (b)
		    fputc(b, f);
	    }
	}
    }
}

/* Prints the queued bands except the last one. 
   If flush is true, even the last one. */
static void print_bands(lxx74_device_t *lxx74, int flush, FILE *f)
{
    band_t *band, *next;
    int left_to_right;
    int x1, fwd, bck = 0;

    while ((band = lxx74->bands_head) && ((next = band->next) || flush)) {
	/* de-queue */
	lxx74->bands_head = next;
	if (next)
	    next->prev = 0;
	else
	    lxx74->bands_tail = 0;

	/* Place in free list so it is somewhere */
	band->next = lxx74->bands_free;
	lxx74->bands_free = band;

	// TODO: initialize dir to 0
	if (lxx74->direction)
	    left_to_right = (lxx74->x < band->x1 - lxx74->accel);
	else
	    left_to_right = (lxx74->x <= band->x2 + lxx74->rtl_adj + lxx74->accel);

	bck = 0;
	if (left_to_right) {
	    x1 = band->x1;
	    fwd = band->x2 + lxx74->accel;
	    if (next && fwd >= next->x1 - lxx74->accel)
		if (fwd < next->x2 + lxx74->rtl_adj + lxx74->accel)
		    fwd = next->x2 + lxx74->rtl_adj + lxx74->accel;
	}
	else {
	    x1 = band->x2 + lxx74->rtl_adj;
	    fwd = band->x1 + lxx74->rtl_adj - lxx74->accel;
	    if (next && fwd <= next->x2 + lxx74->rtl_adj + lxx74->accel)
		if (fwd > next->x1 - lxx74->accel)
		    fwd = next->x1 - lxx74->accel;
	}
	
	/* Output the escape commands */
	
	byte cmd[8];
	cmd[0] = 0x1b;

	if (lxx74->bands_printed++ == 0) {
 	    cmd[1] = 0x21;
	    cmd24(cmd+2, 0);
	    cmd16(cmd+5, 0);
	    checksum(cmd);
	    fwrite(cmd, 1, 8, f);

	    cmd[1] = 0x23;
	    cmd[2] = 0x80;
	    cmd16(cmd+3, band->y*2 + 580); /* Vertical advancement, empirical value */
	    cmd16(cmd+5, 0);       /* Unknown, probably the initial x coordinate */
	    checksum(cmd);
	    fwrite(cmd, 1, 8, f);
	}
	    
	if (band->type == BLACK) {
	    cmd[1] = 0x80;
	    cmd[2] = 0x04;
	    cmd32(cmd+3, band->dots[BLACK]);
	    checksum(cmd);
	    fwrite(cmd, 1, 8, f);
	}
	else {
	    cmd[1] = 0x80;
	    cmd[2] = 0x01;
	    cmd32(cmd+3, band->dots[CYAN]);
	    checksum(cmd);
	    fwrite(cmd, 1, 8, f);

	    cmd[2] = 0x02;
	    cmd32(cmd+3, band->dots[MAGENTA]);
	    checksum(cmd);
	    fwrite(cmd, 1, 8, f);

	    cmd[2] = 0x03;
	    cmd32(cmd+3, band->dots[YELLOW]);
	    checksum(cmd);
	    fwrite(cmd, 1, 8, f);
	}
	cmd[1] = 0x40;
	cmd[2] = 0x10 | (band->type == BLACK ? 0 : 0x80) | (left_to_right ? 1 : 0);
	cmd16(cmd+3, band->x2 - band->x1 + 1);
	cmd16(cmd+5, fwd/4 + 574); /* Fwd: Empirical value */
	checksum(cmd);
	fwrite(cmd, 1, 8, f);
	    
	cmd[1] = 0x42;
	cmd[2] = 0x10;
	cmd16(cmd+3, bck);	/* bck */
	cmd16(cmd+5, next ? (next->y - band->y)*((MAX_Y_RESOLUTION/MIN_Y_RESOLUTION)/lxx74->y_res) : 0); /* down */
	checksum(cmd);
	fwrite(cmd, 1, 8, f);
	    
	cmd[1] = 0x43;
	cmd24(cmd+2, bytes_inklines(lxx74, band));
	cmd16(cmd+5, 9597 + ((MAX_X_RESOLUTION/MIN_X_RESOLUTION)/lxx74->x_res) * x1 ); /* Empirical value */
	checksum(cmd);
	fwrite(cmd, 1, 8, f);

	write_inklines(lxx74, band, left_to_right, f);

	lxx74->x = fwd;
	lxx74->direction = left_to_right;
	fflush(f);
    }
    
}

static int do_preamble(lxx74_device_t *lxx74, FILE *f)
{
    const char *fn = Z33_PREAMBLE_FILENAME;
    FILE *preamble;
    char buf[4096];
    int bl;

    debug("using preamble file '%s'", fn);

    preamble = fopen(fn, "rb");
    if (preamble == NULL){
	error("failed to open preamble file");
	return -1;
    }

    while ((bl = fread(buf, 1, 4096, preamble)) > 0){
	fwrite(buf, 1, bl, f);
    }

    debug("preamble done");
    fclose(preamble);
    fflush(f);
    return 0;
}

byte pageinit_data[] = {
    0x1b, 0x63, 0x00, 0x00, 0xc8, 0x01, 0x72, 0x9e,
    0x1b, 0x30, 0x0c, 0x13, 0x18, 0x4b, 0x92, 0x44,
    0x1b, 0x80, 0x01, 0x00, 0x02, 0xa4, 0x00, 0x27,
    0x1b, 0x80, 0x02, 0x00, 0x02, 0xa4, 0x00, 0x28,
    0x1b, 0x80, 0x03, 0x00, 0x02, 0xa4, 0x00, 0x29,
    0x1b, 0x80, 0x04, 0x00, 0x03, 0x0c, 0x00, 0x93,
};

static int do_pageinit(lxx74_device_t *lxx74, FILE *f)
{
    int wrote = fwrite(pageinit_data, 1, sizeof(pageinit_data), f);
    if (wrote != sizeof(pageinit_data))
	return gs_error_Fatal;
    //    lxx74->bands_printed = 0;
    return 0;
}

// TODO: sometimes printing multiple pages does not work. I suspect that this is because
// the printhead is left too far right after the last (sub)band of the previous page, while
// the next page starts printing left-to-right, assuming the printer is at the far left.
// I once thought that the pageend_data would properly reset the printer for the next
// page, but I probably have to add an empty band to move the head to the far left.

byte pageend_data[] = {
    0x1b, 0x22, 0x80, 0x62, 0x84, 0x00, 0x00, 0x88, 
    0x1b, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 
    0x1b, 0x2a, 0x82, 0x00, 0x00, 0x00, 0x00, 0xac,
};

static int do_pageend(lxx74_device_t *lxx74, FILE *f)
{
    int wrote = fwrite(pageend_data, 1, sizeof(pageend_data), f);
    if (wrote != sizeof(pageend_data))
	return gs_error_Fatal;
    return 0;
}

static char *Arg0;
static cups_page_header2_t *cups_header;
static char *filename;
static long copies;

#define strify(macro) case macro: return #macro

char *cups_adv_name(cups_adv_t adv) 
{
    switch(adv) {
	strify(CUPS_ADVANCE_NONE); /* Never advance the roll */
	strify(CUPS_ADVANCE_FILE); /* Advance the roll after this file */
	strify(CUPS_ADVANCE_JOB);  /* Advance the roll after this job */
	strify(CUPS_ADVANCE_SET);  /* Advance the roll after this set */
	strify(CUPS_ADVANCE_PAGE); /* Advance the roll after this page */
    default: return "(Unknown)";
    }
}

char *cups_cut_name(cups_cut_t cut)
{
    switch(cut) {
	strify(CUPS_CUT_NONE);	/* Never cut the roll */
	strify(CUPS_CUT_FILE);	/* Cut the roll after this file */
	strify(CUPS_CUT_JOB);	/* Cut the roll after this job */
	strify(CUPS_CUT_SET);	/* Cut the roll after this set */
	strify(CUPS_CUT_PAGE);	/* Cut the roll after this page */
    default: return "(Unknown)";
    }
}
char *cups_jog_name(cups_jog_t jog)
{
    switch(jog) {
	strify(CUPS_JOG_NONE);	/* Never move pages */
	strify(CUPS_JOG_FILE);	/* Move pages after this file */
	strify(CUPS_JOG_JOB);	/* Move pages after this job */
	strify(CUPS_JOG_SET);	/* Move pages after this set */
    default: return "(Unknown)";
    }    
}
char *cups_edge_name(cups_edge_t edge)
{
    switch(edge) {
	strify(CUPS_EDGE_TOP);	  /* Leading edge is the top of the page */
	strify(CUPS_EDGE_RIGHT);  /* Leading edge is the right of the page */
	strify(CUPS_EDGE_BOTTOM); /* Leading edge is the bottom of the page */
	strify(CUPS_EDGE_LEFT);	  /* Leading edge is the left of the page */
    default: return "(Unknown)";
    }    
}
char *cups_orient_name(cups_orient_t orient)
{
    switch(orient) {
	strify(CUPS_ORIENT_0);	 /* Don't rotate the page */
	strify(CUPS_ORIENT_90);	 /* Rotate the page counter-clockwise */
	strify(CUPS_ORIENT_180); /* Turn the page upside down */
	strify(CUPS_ORIENT_270); /* Rotate the page clockwise */
    default: return "(Unknown)";
    }    
}

char *cups_order_name(cups_order_t order)
{
    switch(order) {
	strify(CUPS_ORDER_CHUNKED); /* CMYK CMYK CMYK ... */
	strify(CUPS_ORDER_BANDED);  /* CCC MMM YYY KKK ... */
	strify(CUPS_ORDER_PLANAR);  /* CCC ... MMM ... YYY ... KKK ... */
    default: return "(Unknown)";
    }    
}

char *cups_cspace_name(cups_cspace_t cspace)
{
    switch(cspace) {
	strify(CUPS_CSPACE_W);	    /* Luminance */
	strify(CUPS_CSPACE_RGB);    /* Red, green, blue */
	strify(CUPS_CSPACE_RGBA);   /* Red, green, blue, alpha */
	strify(CUPS_CSPACE_K);	    /* Black */
	strify(CUPS_CSPACE_CMY);    /* Cyan, magenta, yellow */
	strify(CUPS_CSPACE_YMC);    /* Yellow, magenta, cyan */
	strify(CUPS_CSPACE_CMYK);   /* Cyan, magenta, yellow, black */
	strify(CUPS_CSPACE_YMCK);   /* Yellow, magenta, cyan, black */
	strify(CUPS_CSPACE_KCMY);   /* Black, cyan, magenta, yellow */
	strify(CUPS_CSPACE_KCMYcm); /* Black, cyan, magenta, yellow, light-cyan, light-magenta     */
	strify(CUPS_CSPACE_GMCK);   /* Gold, magenta, yellow, black */
	strify(CUPS_CSPACE_GMCS);   /* Gold, magenta, yellow, silver */
	strify(CUPS_CSPACE_WHITE);  /* White ink (as black) */
	strify(CUPS_CSPACE_GOLD);   /* Gold foil */
	strify(CUPS_CSPACE_SILVER); /* Silver foil */

	strify(CUPS_CSPACE_CIEXYZ); /* CIE XYZ */
	strify(CUPS_CSPACE_CIELab); /* CIE Lab */
	
	strify(CUPS_CSPACE_ICC1);   /* ICC-based, 1 color */
	strify(CUPS_CSPACE_ICC2);   /* ICC-based, 2 colors */
	strify(CUPS_CSPACE_ICC3);   /* ICC-based, 3 colors */
	strify(CUPS_CSPACE_ICC4);   /* ICC-based, 4 colors */
	strify(CUPS_CSPACE_ICC5);   /* ICC-based, 5 colors */
	strify(CUPS_CSPACE_ICC6);   /* ICC-based, 6 colors */
	strify(CUPS_CSPACE_ICC7);   /* ICC-based, 7 colors */
	strify(CUPS_CSPACE_ICC8);   /* ICC-based, 8 colors */
	strify(CUPS_CSPACE_ICC9);   /* ICC-based, 9 colors */
	strify(CUPS_CSPACE_ICCA);   /* ICC-based, 10 colors */
	strify(CUPS_CSPACE_ICCB);   /* ICC-based, 11 colors */
	strify(CUPS_CSPACE_ICCC);   /* ICC-based, 12 colors */
	strify(CUPS_CSPACE_ICCD);   /* ICC-based, 13 colors */
	strify(CUPS_CSPACE_ICCE);   /* ICC-based, 14 colors */
	strify(CUPS_CSPACE_ICCF);   /* ICC-based, 15 colors */
    default: return "(Unknown)";
    }    
}

static int newpage=0;

/* v a l i d a t e _ p a g e _ h e a d e r */
/* ------------------------------------- */

/* The raster comes from the CUPS pipeline, which normally produces
 * consistent headers - but filters must treat print jobs as untrusted
 * input. Every buffer size below is derived from these fields, so a
 * hostile or buggy header with cupsWidth >> cupsBytesPerLine would
 * cause out-of-bounds reads AND writes in make_bands(), and the
 * mono-path 'bytesperline *= 4' could overflow int. Reject anything
 * inconsistent before any memory is allocated or the printer touched. */
static int validate_page_header(cups_page_header2_t *h)
{
    unsigned w   = h->cupsWidth;
    unsigned bpl = h->cupsBytesPerLine;
    unsigned bpp = h->cupsBitsPerPixel;
    unsigned exp;

    if (w < 1 || w > 65535) {
	error("Bad page header: cupsWidth %u out of range [1,65535]", w);
	return -1;
    }
    if (h->cupsHeight < 1 || h->cupsHeight > 65535) {
	error("Bad page header: cupsHeight %u out of range [1,65535]", h->cupsHeight);
	return -1;
    }
    if (h->HWResolution[0] < 300 || h->HWResolution[0] > 4800 ||
        h->HWResolution[1] < 300 || h->HWResolution[1] > 4800) {
	error("Bad page header: HWResolution %ux%u out of range [300,4800]",
	      h->HWResolution[0], h->HWResolution[1]);
	return -1;
    }

    if (h->cupsColorSpace == CUPS_CSPACE_K) {
	/* 1 bit per pixel, 8 pixels per byte; the filter expands each
	 * input byte to 4 bytes, so bytes per line must be a multiple
	 * of 4 (checked again in lxx74_get_bits). */
	if (bpp != 1) {
	    error("Bad page header: %u bits/pixel unsupported for K (need 1)", bpp);
	    return -1;
	}
	exp = (w + 7) / 8;
    } else if (h->cupsColorSpace == CUPS_CSPACE_CMYK) {
	/* 4 bits per pixel, 2 pixels per byte. */
	if (bpp != 4) {
	    error("Bad page header: %u bits/pixel unsupported for CMYK (need 4)", bpp);
	    return -1;
	}
	exp = (w + 1) / 2;
    } else {
	error("Unsupported colorspace %u", (unsigned)h->cupsColorSpace);
	return -1;
    }

    if (bpl != exp) {
	error("Bad page header: cupsBytesPerLine %u != expected %u for width %u",
	      bpl, exp, w);
	return -1;
    }
    return 0;
}

static int lxx74_print_page(lxx74_device_t *lxx74, FILE *f)
{
    int code = 0;


    {
	int rh = read_cups_header(stdin, cups_header);
	if (rh == 0)
	    return 1; /* clean EOF: no more pages */
	if (rh < 0) {
	    error("%s: read_cups_header failed", Arg0);
	    exit(1);
	}
	if (validate_page_header(cups_header) < 0)
	    exit(1);
    }
    newpage=1;

    if (!lxx74->preamble_done) {
	int retval = do_preamble(lxx74, f);
	if (retval < 0)
	    return retval;
    }
    debug("HWResolution starts at offset %d",(int)((void *)&cups_header->HWResolution[0] - (void *)cups_header));
    debug("MediaClass:      %s", cups_header->MediaClass);
    debug("MediaColor:      %s", cups_header->MediaColor);
    debug("MediaType:       %s", cups_header->MediaType);
    debug("OutputType:      %s", cups_header->OutputType);
    debug("AdvanceDistance: %u", cups_header->AdvanceDistance);
    debug("AdvanceMedia:    %s", cups_adv_name(cups_header->AdvanceMedia));
    debug("Collate:         %s", cups_header->Collate ? "True" : "False");
    debug("CutMedia:        %s", cups_cut_name(cups_header->CutMedia));
    debug("Duplex:          %s", cups_header->Duplex ? "True" : "False");
    debug("HWResolution[0]: %u", cups_header->HWResolution[0]);
    debug("HWResolution[1]: %u", cups_header->HWResolution[1]);
    debug("ImagingBoundingBox[0]: %u", cups_header->ImagingBoundingBox[0]);
    debug("ImagingBoundingBox[1]: %u", cups_header->ImagingBoundingBox[1]);
    debug("ImagingBoundingBox[2]: %u", cups_header->ImagingBoundingBox[2]);
    debug("ImagingBoundingBox[3]: %u", cups_header->ImagingBoundingBox[3]);
    debug("InsertSheet      %s", cups_header->InsertSheet ? "True" : "False");
    debug("Jog:             %s", cups_jog_name(cups_header->Jog));
    debug("LeadingEdge:     %s", cups_edge_name(cups_header->LeadingEdge));
    debug("Margins[0]       %u", cups_header->Margins[0]);
    debug("Margins[1]       %u", cups_header->Margins[1]);
    debug("ManualFeed       %s", cups_header->ManualFeed ? "True" : "False");
    debug("MediaPosition:   %u", cups_header->MediaPosition);
    debug("MediaWeight:     %u", cups_header->MediaWeight);
    debug("MirrorPrint:     %s", cups_header->MirrorPrint ? "True" : "False");
    debug("NegativePrint:   %s", cups_header->NegativePrint ? "True" : "False");
    debug("NumCopies:       %u", cups_header->NumCopies);
    debug("Orientation:     %s", cups_orient_name(cups_header->Orientation));
    debug("OutputFaceUp     %s", cups_header->OutputFaceUp ? "True" : "False");
    debug("PageSize[0]      %u", cups_header->PageSize[0]);
    debug("PageSize[1]      %u", cups_header->PageSize[1]);

    debug("cupsWidth        %u", cups_header->cupsWidth); /* Width of page image in pixels */
    debug("cupsHeight       %u", cups_header->cupsHeight);/* Height of page image in pixels */
    debug("cupsMediaType    %u", cups_header->cupsMediaType);/* Media type code */
    debug("cupsBitsPerColor %u", cups_header->cupsBitsPerColor);/* Number of bits for each color */
    debug("cupsBitsPerPixel %u", cups_header->cupsBitsPerPixel);/* Number of bits for each pixel */
    debug("cupsBytesPerLine %u", cups_header->cupsBytesPerLine);/* Number of bytes per line */

    debug("cupsColorOrder:  %s", cups_order_name(cups_header->cupsColorOrder));/* Order of colors */
    debug("cupsColorSpace:  %s", cups_cspace_name(cups_header->cupsColorSpace));/* True colorspace */

    debug("cupsCompression  %u", cups_header->cupsCompression);/* Device compression to use */
    debug("cupsRowCount     %u", cups_header->cupsRowCount);/* Rows per band */
    debug("cupsRowFeed      %u", cups_header->cupsRowFeed);/* Feed between bands */
    debug("cupsRowStep      %u", cups_header->cupsRowStep);/* Spacing between lines */


    lxx74->HWResolution[0] = cups_header->HWResolution[0];
    lxx74->HWResolution[1] = cups_header->HWResolution[1];
    debug("Resolution is %8.2f x%8.2f",lxx74->HWResolution[0],lxx74->HWResolution[1]);
    lxx74->width = cups_header->cupsWidth;
    lxx74->height = cups_header->cupsHeight;
    lxx74->bytesperline = cups_header->cupsBytesPerLine;
    if (lxx74->pix)
	finalize_pixbuf(lxx74);
    initialize_pens_and_pixbuf(lxx74);

    if (cups_header->cupsColorSpace == CUPS_CSPACE_K){
	lxx74->mono = 1;
	lxx74->bytesperline *= 4;
    } else
    if (cups_header->cupsColorSpace == CUPS_CSPACE_CMYK){
	lxx74->mono = 0;
    } else {
	error("Unsupporter colorspace: %s", cups_cspace_name(cups_header->cupsColorSpace));
	return -1;
    }

    if ((code = do_pageinit(lxx74, f)))
	return code;
    
    code = setjmp(errorjmp);
    if (!code) {
	/* The header-parsing section above already allocated a pixbuf;
	 * release it before re-initializing (avoids leaking one per page). */
	finalize_pixbuf(lxx74);
	initialize_pens_and_pixbuf(lxx74);

	get_scanlines(lxx74);
    
	while (feed_scanlines(lxx74) == 0) {
	    make_bands(lxx74);
	    print_bands(lxx74, 0, f);
	}
	print_bands(lxx74, 1, f);
	do_pageend(lxx74, f);
    }

    fflush(f);

    /* Deallocate all extra memory */

    while (lxx74->bands_free) {
	band_t *band = lxx74->bands_free;
	lxx74->bands_free = band->next;
	finalize_band(lxx74, band);
    }
    finalize_pixbuf(lxx74);

    if (code < 0)
	return code;
    return code;
}


#include <malloc.h>
static void *lxx74_malloc(size_t size)
{
    void *mem = malloc(size);
    if(!mem)
	longjmp(errorjmp, gs_error_VMerror);
    memset(mem, 0, size);
    return mem;
}

static void lxx74_free(void *mem)
{
    free(mem);
}

static int lxx74_open(lxx74_device_t *lxx74)
{
    byte *p, *q;
    p = (byte*) &lxx74->y_res;
    q = (byte*) (&lxx74[1]);
    bzero(p, q-p);

    /* If this is changed, it must be changed everywhere. Search for 'initialized'. */
    if (lxx74->initialized == 0) {
	lxx74->rtl_adj = DEFAULT_RTL_ADJUST;
	lxx74->initialized = 1;
	lxx74->accel = ACCELLERATION_MARGIN;
	lxx74->HWResolution[0] = 600.0;
	lxx74->HWResolution[1] = 600.0;
	
	lxx74->width = 4958;
	lxx74->height = 7017;
    }

    if (filename && strcmp(filename,"-") && !freopen(filename, "r", stdin)) {
	error("Cannot open file %s: %s", filename, strerror(errno));
	exit(1);
    }
    
    {
	struct stat st;
	int result = fstat(fileno(stdin), &st);
	if (result >= 0) {
	    debug("File %s: %jd bytes", filename, (intmax_t)st.st_size);
	}
    }
    

    if (!cups_header) {
	cups_header = (cups_page_header2_t *)malloc(sizeof(cups_page_header2_t));
	if (!cups_header) {
	    error("%s: Out of memory", Arg0);
	    exit(1);
	}
    }

    return 0;
}

static int lxx74_close(lxx74_device_t *lxx74)
{
    finalize_pixbuf(lxx74);
    if (lxx74->scanbuf) {
	lxx74_free(lxx74->scanbuf);
	lxx74->scanbuf = 0;
    }
    if (cups_header) {
	free(cups_header);
	cups_header = 0;
    }
    return 0;
}

static int lxx74_get_bits(lxx74_device_t *lxx74, int y, byte *buffer, byte **bufptr)
{
    int got;
    
    if (newpage) debug("Starting page at byte %jd", (intmax_t)ftell(stdin));
    newpage = 0;

    if (lxx74->mono){
	int blks = lxx74->bytesperline / 4;
	int tail = lxx74->bytesperline - blks*4;
	int i;
	unsigned char p;

	if (tail != 0){
		error("TODO ? non-zero tail");
		return -1;
	}

	got = fread(buffer, 1, blks, stdin);

	if (got < 0)
	    return got;
	if (got == 0)
	    return LXX74_EOF;
	if (got < blks)
	    return -2;

	for (i=got-1;i>=0;i--){
	    p = buffer[i];
	    buffer[i*4+3] = (((p>>0) & 1) << BLACK) |
			   ((((p>>1) & 1) << BLACK) << 4);
	    buffer[i*4+2] = (((p>>2) & 1) << BLACK) |
			   ((((p>>3) & 1) << BLACK) << 4);
	    buffer[i*4+1] = (((p>>4) & 1) << BLACK) |
			   ((((p>>5) & 1) << BLACK) << 4);
	    buffer[i*4]   = (((p>>6) & 1) << BLACK) |
			   ((((p>>7) & 1) << BLACK) << 4);

	}


	*bufptr = buffer;
	return 0;
    }

    got = fread(buffer, 1, lxx74->bytesperline, stdin);

    if (got < 0)
	return got;
    if (got == 0)
	return LXX74_EOF;
    if (got < lxx74->bytesperline)
	return -2;
    *bufptr = buffer;
    return 0;
}

// Parse a string of options, or an array of option strings (one
// options per string).

// create an array of options from an options string

char **optarray(char *opts)
{
    int count = 0;
    char *cp;
    char **ret, **stp;

    cp =opts;

    while (*cp) {
	while (*cp == ' ' || *cp == '\t' || *cp == '\n')
	    ++cp;
	if (!*cp)
	    break;
	++count;
	while (*cp && *cp != ' ' && *cp != '\t' && *cp != '\n')
	    ++cp;
    }

    ret = calloc(count + 1, sizeof(char *));

    if ((stp = ret)) { /* Assignment! */
	cp = opts;
	while (*cp) {
	    while (*cp == ' ' || *cp == '\t' || *cp == '\n')
		++cp;
	    if (!*cp)
		break;
	    *stp++ = cp++;
	    while (*cp && *cp != ' ' && *cp != '\t' && *cp != '\n')
		++cp;
	    if (*cp)
		*cp++ = 0;
	}
	*stp = 0;
    }

    return ret;
}

typedef struct optiondata_s
{
    char *optname;
    int takesarg;
    int offset;
    int min;
    int max;
} optiondata_t;

#define LXX74_MEMBER_OFFSET(member) ( (int) (uintptr_t) & ((lxx74_device_t *)0)->member )
optiondata_t optiondata[] = {
    { "BlackAdjustX=", 1, LXX74_MEMBER_OFFSET(black_adj_x), -25, 25 }, 
    { "BlackAdjustY=", 1, LXX74_MEMBER_OFFSET(black_adj_y), -25, 25 },
    { "RTLAdjust=",    1, LXX74_MEMBER_OFFSET(rtl_adj),     -25, 25 },
    { "WidthPixels=",  1, LXX74_MEMBER_OFFSET(width),   -25, 25 },
    { "HeightPixels=", 1, LXX74_MEMBER_OFFSET(height),  -25, 25 },
    { NULL,            0, 0,                                     0,  0 }
};


/* Return number of options processed */
int process_options(lxx74_device_t *lxx74, char** opt_array, int expect_hyphens)
{
    char **opt_p = opt_array;
    char *opt, *endp;
    optiondata_t *odp;

    while ((opt = *opt_p++)) {
	if (expect_hyphens) {
	    if (*opt == '-' && opt[1] == 'd')
		opt += 2;
	    else if (*opt == '-')
		++opt;
	    else
		break;
	}
	for (odp = optiondata; odp->optname; ++odp) {
	    int len = strlen(odp->optname);
	    if (strncmp(opt, odp->optname, len) == 0) {
		if (opt[len-1] == '=') {
		    int val = strtol(opt+len, &endp, 0);
		    if (*endp) {
			error("Illegal characters in option value: %s", opt);
			break;
		    }
		    if (val < odp->min || val > odp->max) {
			error("Option %s value outside range [%d,%d]", opt, odp->min, odp->max);
			break;
		    }
		    *(int *)((void *)lxx74 + odp->offset) = val;
		}
		else
		    *(int *)((void *)lxx74 + odp->offset) = 1; //-cups
		break;
	    }
	}
	if (!odp->optname)
	    debug("Ignoring non-driver option %s", opt);
    }
    return opt_p - opt_array;
}

static void lxx74_init(lxx74_device_t *lxx74, int argc, char *argv[])
{
    char *options = NULL;

    /* If this is changed, it must be changed everywhere. Search for 'initialized'. */
    if (lxx74->initialized == 0) {
	lxx74->rtl_adj = DEFAULT_RTL_ADJUST;
	lxx74->accel = ACCELLERATION_MARGIN;

	lxx74->HWResolution[0] = 600.0;
	lxx74->HWResolution[1] = 600.0;
	
	lxx74->width = 4958;
	lxx74->height = 7017;

	lxx74->initialized = 1;
    }

    if (argc < 6) {
	error("Need at least 6 args: printer job user title copies options [filename]");
	exit(1);
    }
    /* CUPS filters are invoked in two ways:
     *  - directly by cupsd:  [path] printer job user title copies options [filename]
     *  - chained by an upstream filter (observed with cups-filters 1.28
     *    gstoraster): printer job user title copies options  (argv[0] is the
     *    PRINTER name, there is no program path and no filename; the data
     *    arrives on stdin).
     * Detect the shape by argc and, for the ambiguous argc==6 case, by
     * whether the last argument looks like an options string. */
    if (argc >= 8) {
	char *endptr;
	copies = strtol(argv[5], &endptr, 0);
	options = argv[6];
	filename = argv[7];
    }
    else if (argc == 7) {
	char *endptr;
	copies = strtol(argv[5], &endptr, 0);
	options = argv[6];
	filename = NULL;
    }
    else if (argc == 6) {
	char *endptr;
	if (strchr(argv[5], '=') != NULL) {
	    /* chained invocation */
	    copies = strtol(argv[4], &endptr, 0);
	    options = argv[5];
	} else {
	    copies = strtol(argv[5], &endptr, 0);
	    options = NULL;
	}
	filename = NULL;
    }
    else {
	error("Need 6-8 args: [prog] printer job user title copies options [filename]");
	exit(1);
    }
    if (options && *options) {
	char ** opt_array = optarray(options);
	debug("Found options on the command line: '%s'", options);
	if (opt_array) {
	    process_options(lxx74, opt_array, 0);
	    free(opt_array);
	} else
	    error("Could not parse optons string");
    } else {
	char *serverroot = getenv("CUPS_SERVERROOT");
	char *printer = getenv("PRINTER");
	char buffer[1024];
	
	debug("Looking for lpoptions file");
	if (serverroot && printer) {
	    char *filename = malloc(strlen(serverroot) + strlen("/lpoptions") + 1);
	    if (filename) {
		FILE *lpoptions;
		strcat(strcpy(filename, serverroot), "/lpoptions");
		lpoptions = fopen(filename, "r");
		if (lpoptions) {
		    debug("Opened file %s", filename);
		    while (fgets(buffer, sizeof(buffer), lpoptions)) {
			char optprinter[256];
			char dest[12];
			char ** opt_array;
			int count;
			int convs = sscanf(buffer, "%11s%255s%n", dest, optprinter, &count);
			if (convs < 2 || strcasecmp(dest, "Dest") || strcmp(optprinter, printer))
			    continue;
			opt_array = optarray(buffer + count);
			if (opt_array) {
			    process_options(lxx74, opt_array, 0);
			    free(opt_array);
			} else
			    error("Unable to parse options %s", buffer + count);
		    }
		    fclose(lpoptions);
		} else if (errno != ENOENT)
		    error("Could not open %s: %s", filename, strerror(errno));
		else
		    debug("No server-wide lpoptions file (%s); using defaults.", filename);
		free(filename);
	    } else
		error("Out of memory");
	} else
	    debug("No lpoptions adjustments available (CUPS_SERVERROOT/PRINTER env not set); using defaults.");
    }
}

int main(int argc, char *argv[])
{
    int code;

    
    Arg0 = argv[0];

#ifndef DRIVER_VERSION
#define DRIVER_VERSION "dev"
#endif
    fprintf(stderr, "DEBUG: rastertolxz33 v%s (CUPS filter for Lexmark Z23/Z33 inkjets,\n", DRIVER_VERSION);
    fprintf(stderr, "DEBUG: based on lxx74-cups-0.8.2-z33 by Enrique Perez-Terron and Vitaly V. Bursov).\n");
    fprintf(stderr, "DEBUG: invoked with argc=%d:", argc);
    for (int i = 0; i < argc; i++)
	fprintf(stderr, " [%d]=%s", i, argv[i] ? argv[i] : "(null)");
    fprintf(stderr, "\n");

    lxx74_device_t *lxx74 = malloc(sizeof(*lxx74));
    if (!lxx74) {
	error("%s: Out of memory", argv[0]);
	exit(1);
    }

    bzero(lxx74, sizeof(*lxx74));
    lxx74_init(lxx74, argc, argv);
    lxx74_open(lxx74);

    /* print_page() returns nonzero at end of stream. */
    while ((code = lxx74_print_page(lxx74, stdout)) == 0)
	;
    lxx74_close(lxx74);
    free(lxx74);
    exit(0);
}

/* Emacs: */
/* Local Variables: */
/* mode: c */
/* c-basic-offset: 4 */
/* fill-column: 90 */
/* End: */
