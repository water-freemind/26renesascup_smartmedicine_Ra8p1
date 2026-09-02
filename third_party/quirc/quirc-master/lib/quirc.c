/* quirc -- QR-code recognition library
 * Copyright (C) 2010-2012 Daniel Beer <dlbeer@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include "quirc_internal.h"

/* RA8P1 port: the C library heap is disabled (BSP_CFG_HEAP_BYTES=0), so quirc's
 * malloc/calloc/free calls are redirected to a static SDRAM pool.
 *
 * Host 端调试（PC 上用本目录源码复现解码）编译时定义 QUIRC_HOST_BUILD，
 * 使用标准 libc malloc，见 tools/host_quirc_decode 用法。
 *
 * Why SDRAM instead of the FreeRTOS heap?  The QR decoder now upscales the
 * 320x240 camera frame 2x and decodes 640x480: the image buffer alone is
 * 307 KB, which does not fit the 256 KB FreeRTOS heap (configTOTAL_HEAP_SIZE).
 * quirc allocates once per quirc_resize() and keeps the buffers for the whole
 * lifetime of the quirc object, so a simple bump allocator over a reserved
 * SDRAM region is sufficient (free() is a no-op; quirc_destroy() releases
 * nothing, which is fine for the single-decoder design).
 *
 * 0x68576000 is past ALL SDRAM regions owned by the firmware (VGA build):
 *   - frame buffers / TTF RX:      ~0x68000000 - 0x68368610
 *   - CEU double buffers (2x614KB): 0x68367E00 - 0x68493E00
 *   - qr_decoder (s_gray/snapshot/smooth ~921KB): 0x68494620 - 0x68575620
 * (see the linker map; SDRAM is 16 MB, 0x68000000 - 0x68100000).
 * Pool sized for one 640x480 image (307KB) + flood-fill vars + structs. */
#include <string.h>

#ifndef QUIRC_HOST_BUILD
/* Pool 基址必须在 .sdram_noinit 段之后（qr_decoder 的 s_gray/snapshot/roi/
 * smooth 共 934KB 延伸到 0x68578820，见 map；0x68600000 留 0x28000 余量）。
 * SDRAM 共 128MB（0x68000000-0x70000000），pool 1MB 无冲突。
 * 两个 quirc 实例：全画面 640x480 ≈ 311KB + 中央 ROI 400x400 ≈ 163KB。 */
#define QUIRC_SDRAM_POOL_BASE   (0x68600000U)
#define QUIRC_SDRAM_POOL_SIZE   (0x00100000U)   /* 1 MB reserved */

static void * qr_port_malloc(size_t size)
{
    static uint32_t s_offset = 0U;
    void * p = (void *) (QUIRC_SDRAM_POOL_BASE + s_offset);
    s_offset += (uint32_t) ((size + 31U) & ~((size_t) 31U));
    if (s_offset > QUIRC_SDRAM_POOL_SIZE)
    {
        return NULL;   /* pool exhausted (should never happen) */
    }
    return p;
}

static void * qr_port_calloc(size_t count, size_t size)
{
    void * p = qr_port_malloc(count * size);
    if (NULL != p)
    {
        memset(p, 0, count * size);
    }
    return p;
}

static void qr_port_free(void * p)
{
    (void) p;   /* bump allocator: nothing to release */
}

#define malloc(size)      qr_port_malloc(size)
#define calloc(n, size)   qr_port_calloc((n), (size))
#define free(ptr)         qr_port_free(ptr)
#endif /* QUIRC_HOST_BUILD */

const char *quirc_version(void)
{
	return "1.0";
}

struct quirc *quirc_new(void)
{
	struct quirc *q = malloc(sizeof(*q));

	if (!q)
		return NULL;

	memset(q, 0, sizeof(*q));
	return q;
}

void quirc_destroy(struct quirc *q)
{
	free(q->image);
	/* q->pixels may alias q->image when their type representation is of the
	   same size, so we need to be careful here to avoid a double free */
	if (!QUIRC_PIXEL_ALIAS_IMAGE)
		free(q->pixels);
	free(q->flood_fill_vars);
	free(q);
}

int quirc_resize(struct quirc *q, int w, int h)
{
	uint8_t		*image  = NULL;
	quirc_pixel_t	*pixels = NULL;
	size_t num_vars;
	size_t vars_byte_size;
	struct quirc_flood_fill_vars *vars = NULL;

	/*
	 * XXX: w and h should be size_t (or at least unsigned) as negatives
	 * values would not make much sense. The downside is that it would break
	 * both the API and ABI. Thus, at the moment, let's just do a sanity
	 * check.
	 */
	if (w < 0 || h < 0)
		goto fail;

	/*
	 * alloc a new buffer for q->image. We avoid realloc(3) because we want
	 * on failure to be leave `q` in a consistant, unmodified state.
	 */
	image = calloc(w, h);
	if (!image)
		goto fail;

	/* compute the "old" (i.e. currently allocated) and the "new"
	   (i.e. requested) image dimensions */
	size_t olddim = q->w * q->h;
	size_t newdim = w * h;
	size_t min = (olddim < newdim ? olddim : newdim);

	/*
	 * copy the data into the new buffer, avoiding (a) to read beyond the
	 * old buffer when the new size is greater and (b) to write beyond the
	 * new buffer when the new size is smaller, hence the min computation.
	 */
	(void)memcpy(image, q->image, min);

	/* alloc a new buffer for q->pixels if needed */
	if (!QUIRC_PIXEL_ALIAS_IMAGE) {
		pixels = calloc(newdim, sizeof(quirc_pixel_t));
		if (!pixels)
			goto fail;
	}

	/*
	 * alloc the work area for the flood filling logic.
	 *
	 * the size was chosen with the following assumptions and observations:
	 *
	 * - rings are the regions which requires the biggest work area.
	 * - they consumes the most when they are rotated by about 45 degree.
	 *   in that case, the necessary depth is about (2 * height_of_the_ring).
	 * - the maximum height of rings would be about 1/3 of the image height.
	 */

	if ((size_t)h * 2 / 2 != h) {
		goto fail; /* size_t overflow */
	}
	num_vars = (size_t)h * 2 / 3;
	if (num_vars == 0) {
		num_vars = 1;
	}

	vars_byte_size = sizeof(*vars) * num_vars;
	if (vars_byte_size / sizeof(*vars) != num_vars) {
		goto fail; /* size_t overflow */
	}
	vars = malloc(vars_byte_size);
	if (!vars)
		goto fail;

	/* alloc succeeded, update `q` with the new size and buffers */
	q->w = w;
	q->h = h;
	free(q->image);
	q->image = image;
	if (!QUIRC_PIXEL_ALIAS_IMAGE) {
		free(q->pixels);
		q->pixels = pixels;
	}
	free(q->flood_fill_vars);
	q->flood_fill_vars = vars;
	q->num_flood_fill_vars = num_vars;

	return 0;
	/* NOTREACHED */
fail:
	free(image);
	free(pixels);
	free(vars);

	return -1;
}

int quirc_count(const struct quirc *q)
{
	return q->num_grids;
}

static const char *const error_table[] = {
	[QUIRC_SUCCESS] = "Success",
	[QUIRC_ERROR_INVALID_GRID_SIZE] = "Invalid grid size",
	[QUIRC_ERROR_INVALID_VERSION] = "Invalid version",
	[QUIRC_ERROR_FORMAT_ECC] = "Format data ECC failure",
	[QUIRC_ERROR_DATA_ECC] = "ECC failure",
	[QUIRC_ERROR_UNKNOWN_DATA_TYPE] = "Unknown data type",
	[QUIRC_ERROR_DATA_OVERFLOW] = "Data overflow",
	[QUIRC_ERROR_DATA_UNDERFLOW] = "Data underflow"
};

const char *quirc_strerror(quirc_decode_error_t err)
{
	if (err >= 0 && err < sizeof(error_table) / sizeof(error_table[0]))
		return error_table[err];

	return "Unknown error";
}
