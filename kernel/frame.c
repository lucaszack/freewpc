/*
 * Copyright 2006-2009 by Brian Dominy <brian@oddchange.com>
 *
 * This file is part of FreeWPC.
 *
 * FreeWPC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FreeWPC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FreeWPC; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * \file
 * \brief Render full-sized DMD frames.
 *
 * This module provides an API for drawing full-screen 128x32 4-color images
 * to the dot matrix that have been compressed into the FreeWPC Image Format
 * (FIF).  The fiftool utility can be used to generate FIFs from PGMs, which
 * are supported by most graphics programs.  The key feature of the FIF is
 * simple compression, since WPC provides limited space and has limited
 * processing cycles.
 */

#include <freewpc.h>
#include <xbmprog.h>

/**
 * The way that images are accessed is very different in 6809 vs. native mode.
 */

#ifdef CONFIG_NATIVE
#undef IMAGEMAP_BASE
U8 IMAGEMAP_BASE[262144];

struct frame_pointer
{
	U8 ptr_hi;
	U8 ptr_lo;
	U8 page;
} __attribute__((packed));

#define PTR(p) (&IMAGEMAP_BASE[((p->ptr_hi - 0x40) * 256UL + p->ptr_lo) + (p->page * 0x4000UL)])

#else /* 6809 */

struct frame_pointer
{
	unsigned char *ptr;
	U8 page;
};

#define PTR(p) (p->ptr)

#endif

U8 frame_repeat_count;

U8 frame_repeat_value;


static __attribute__((noinline))
const U8 *frame_copy_raw (const U8 *framedata)
{
	dmd_copy_page (dmd_low_buffer, (const dmd_buffer_t)framedata);
	return framedata + DMD_PAGE_SIZE;
}


static __attribute__((noinline))
const U8 *frame_copy_rle (const U8 *framedata)
{
	register U8 *dbuf = dmd_low_buffer;
	register U8 c;

	do {
		c = *framedata++;
		if (c == XBMPROG_RLE_SKIP)
		{
			/* The 'skip' flag indicates an RLE sequence where
			the data byte is assumed to be zero.  The zero byte
			is not present in the stream.  The zero case occurs
			frequently, and is thus given special treatment. */
			frame_repeat_count = *framedata++;

			while (frame_repeat_count >= 4)
			{
				*dbuf++ = 0;
				*dbuf++ = 0;
				*dbuf++ = 0;
				*dbuf++ = 0;
				frame_repeat_count -= 4;
			}

			while (frame_repeat_count != 0)
			{
				*dbuf++ = 0;
				frame_repeat_count--;
			}
		}
		else if (c == XBMPROG_RLE_REPEAT)
		{
			/* The 'repeat' flag is the usual RLE case and can
			support a sequence of any byte value. */
			frame_repeat_value = *framedata++; /* data */
			frame_repeat_count = *framedata++; /* count */
			/* TODO - use word copies if possible */
			do {
				*dbuf++ = frame_repeat_value;
			} while (--frame_repeat_count != 0);
		}
		else
			/* Unrecognized flags are interpreted as literals.
			Note that a literal value that matches a flag value
			above will need to be encoded as an RLE sequence of
			1, since no escape character is defined. */
			*dbuf++ = c;
	} while (unlikely (dbuf < dmd_low_buffer + DMD_PAGE_SIZE));
	return framedata;
}


static __attribute__((noinline))
const U8 *frame_xor_rle (const U8 *framedata)
{
	register U8 *dbuf = dmd_low_buffer;
	register U8 c;

	do {
		c = *framedata++;
		if (c == XBMPROG_RLE_SKIP)
		{
			dbuf += *framedata++;
		}
		else if (c == XBMPROG_RLE_REPEAT)
		{
			frame_repeat_value = *framedata++; /* data */
			frame_repeat_count = *framedata++; /* count */
			do {
				*dbuf++ ^= frame_repeat_value;
				--frame_repeat_value;
			} while (frame_repeat_value != 0);
		}
		else
			*dbuf++ ^= c;
	} while (unlikely (dbuf < dmd_low_buffer + DMD_PAGE_SIZE));
	return framedata;
}


/** An internal function to decompress a single bitplane
 * into the low-mapped page buffer. */
static const U8 *dmd_decompress_bitplane (const U8 *framedata)
{
	U8 method;

	/* TODO - remove this and force all callers of the
	function to do the appropriate save/restore. */
	page_push (FIF_PAGE);

	/* The first byte of a compressed bitplane is a 'method', which
	says the overlap manner in which the image has been
	compressed. */
	method = *framedata++;
	switch (method)
	{
		case XBMPROG_METHOD_RAW:
			/* In the 'raw' method, no compression was done at
			all.  The following 512 bytes are copied verbatim to
			the display buffer. */
			framedata = frame_copy_raw (framedata);
			break;

		case XBMPROG_METHOD_RLE:
			/* In the 'run-length encoding (RLE)' method,
			certain long sequences of the same byte are replaced
			by a flag, the byte, and a count. */
			framedata = frame_copy_rle (framedata);
			break;

		case XBMPROG_METHOD_RLE_DELTA:
			/* The RLE delta method is almost identical to the
			RLE method above, but the input stream is overlaid on
			top of the existing image data, using XOR operations
			instead of simple assignment.  This is useful for animations
			in which a subsequent frame is quite similar to its
			predecessor. */
			framedata = frame_xor_rle (framedata);
			break;
	}

	page_pop ();
	return framedata;
}


/** Draws a FreeWPC formatted image (FIF), which is just a
 * compressed XBMPROG.  The image header says whether or not
 * it is 1-bit or 2-bit. */
const U8 *dmd_draw_fif1 (const U8 *fif)
{
	U8 depth;

	/* The first byte of the FIF format is the depth, which
	 * indicates if the frame has 2 or 4 colors. */
	page_push (FIF_PAGE);
	depth = *fif++;
	page_pop ();

	/* Draw the frame(s) */
	fif = dmd_decompress_bitplane (fif);
	if (depth == 2)
	{
		task_yield ();
		dmd_flip_low_high ();
		fif = dmd_decompress_bitplane (fif);
		dmd_flip_low_high ();
	}
	return fif;
}


#ifdef IMAGEMAP_PAGE

#ifndef __m6809__
void frame_decode_rle_c (U8 *data)
{
	U16 *src = (U16 *)data;
	U16 *dst = (U16 *)dmd_low_buffer;

	while ((U8 *)dst < dmd_low_buffer + 512)
	{
		U16 val = *src++;
		if ((val & 0xFF00) == 0xA800)
		{
			U8 words = val & 0xFF;
			U8 repeater = *((U8 *)src);
			U16 repeated_word = repeater | ((U16)repeater << 8);
			src = (U16 *)((U8 *)src + 1);
			while (words > 0)
			{
				*dst++ = repeated_word;
				words--;
			}
		}
		else
		{
			*dst++ = val;
		}
	}
}
#endif


/**
 * Decode the source of a DMD frame.  DATA points to the
 * source data; the ROM page is already mapped.  TYPE
 * says how it was encoded.
 */
void frame_decode (U8 *data, U8 type)
{
	if (type == 0)
	{
		dmd_copy_page (dmd_low_buffer, (const dmd_buffer_t)data);
	}
	else if (type == 2)
	{
		frame_decode_rle (data);
	}
	else if (type == 4)
	{
		frame_decode_sparse (data);
	}
}


/**
 * Draw one plane of a DMD frame.
 * ID identifies the source of the frame data.
 * The output is always drawn to the low-mapped buffer.
 */
void frame_draw_plane (U16 id)
{
	/* Lookup the image number in the global table.
	 * For real ROMs, this is located at a fixed address.
	 * In native mode, the images are kept in a separate file.
	 */
	U8 type;
	struct frame_pointer *p;
	U8 *data;

	page_push (IMAGEMAP_PAGE);
	p = (struct frame_pointer *)IMAGEMAP_BASE + id;
	data = PTR(p);

	/* Switch to the page containing the image data.
	 * Pull the type byte out, then decode the remaining bytes
	 * to the display buffer. */
	pinio_set_bank (PINIO_BANK_ROM, p->page);
	type = data[0];
	frame_decode (data + 1, type & ~0x1);

	page_pop ();
}


/**
 * Draw a 2-plane, 4-color DMD frame.
 * ID identifies the first plane of the frame.  The two
 * frames have consecutive IDs.
 */
void frame_draw (U16 id)
{
	frame_draw_plane (id++);
	dmd_flip_low_high ();
	frame_draw_plane (id);
	dmd_flip_low_high ();
}


/**
 * Draw an arbitrary sized bitmap at a particular region
 * of the display.
 */
void bmp_draw (U8 x, U8 y, U16 id)
{
	struct frame_pointer *p;

	page_push (IMAGEMAP_PAGE);
	p = (struct frame_pointer *)IMAGEMAP_BASE + id;
	page_push (p->page);
	bitmap_blit (PTR(p) + 1, x, y);
	if (PTR(p)[0] & 0x1)
	{
		dmd_flip_low_high ();
		p++;
		bitmap_blit (PTR(p) + 1, x, y);
		dmd_flip_low_high ();
	}
	page_pop ();
	page_pop ();
}


CALLSET_ENTRY (frame, init)
{
	/* In native mode, read the images from an external file into memory. */
#ifdef CONFIG_NATIVE
	FILE *fp;
	const char *filename = "build/" MACHINE_SHORTNAME "_images.rom";
	fp = fopen (filename, "rb");
	if (!fp)
	{
		dbprintf ("Cannot open image file %s", filename);
		return;
	}
	fread (IMAGEMAP_BASE, sizeof (U8), 262144, fp);
#endif
}

#endif /* IMAGEMAP_PAGE */

