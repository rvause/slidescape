/*
  BSD 2-Clause License

  Copyright (c) 2019-2026, Pieter Valkema

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// To compile without implementing stb_printf.h:
// #define LIBISYNTAX_NO_STB_SPRINTF_IMPLEMENTATION

// To compile without implementing stb_image.h.:
// #define LIBISYNTAX_NO_STB_IMAGE_IMPLEMENTATION

// To compile without implementing thread pool routines (in case you want to supply your own):
// #define LIBISYNTAX_NO_THREAD_POOL_IMPLEMENTATION

#if __has_include("config.h")
#include "config.h"
#endif

#ifndef LIBISYNTAX_NO_STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_IMPLEMENTATION
#endif
#include "stb_sprintf.h"

#ifndef LIBISYNTAX_NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

#include "common.h"
#include "platform.h"
#include "intrinsics.h"

#include "libisyntax.h"
#include "isyntax.h"
#include "isyntax_reader.h"
#include <math.h>

#define CHECK_LIBISYNTAX_OK(_libisyntax_call) do { \
    isyntax_error_t result = _libisyntax_call;     \
    ASSERT(result == LIBISYNTAX_OK);               \
} while(0);

// Routines for initializing the global thread pool

// TODO(avirodov): int may be too small for some counters later on.
// TODO(avirodov): should make a flag to turn counters off, they may have overhead.
// TODO(avirodov): struct? move to isyntax.h/.c?
// TODO(avirodov): debug api?
#define DBGCTR_COUNT(_counter) atomic_increment(&_counter)
i32 volatile dbgctr_init_thread_pool_counter = 0;

static platform_mutex_t libisyntax_global_mutex = PLATFORM_MUTEX_INITIALIZER;

isyntax_error_t libisyntax_init() {
    // Lock-unlock to ensure that all parallel calls to libisyntax_init() wait for the actual initialization to complete.
    platform_mutex_lock(&libisyntax_global_mutex);
    static bool libisyntax_global_init_complete = false;

    if (libisyntax_global_init_complete == false) {
        // Actual initialization.
#ifndef LIBISYNTAX_THREAD_POOL_SHARED_WITH_SLIDESCAPE
        init_global_system_info(false);
        DBGCTR_COUNT(dbgctr_init_thread_pool_counter);
        init_thread_pool(&global_thread_pool,
                         1024,
                         false,
                         false,
                         NULL);

#else
        libisyntax_init_thread_pool_for_slidescape();
#endif
        libisyntax_global_init_complete = true;
    }
    platform_mutex_unlock(&libisyntax_global_mutex);
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_open(const char* filename, enum libisyntax_open_flags_t flags, isyntax_t** out_isyntax) {
    // Note(avirodov): intentionally not changing api of isyntax_open. We can do that later if needed and reduce
    // the size/count of wrappers.
    isyntax_t* result = malloc(sizeof(isyntax_t));
    memset(result, 0, sizeof(*result));

    bool success = isyntax_open(result, filename, flags);
    if (success) {
        *out_isyntax = result;
        return LIBISYNTAX_OK;
    } else {
        free(result);
        return LIBISYNTAX_FATAL;
    }
}

void libisyntax_close(isyntax_t* isyntax) {
    isyntax_destroy(isyntax);
    free(isyntax);
}

int32_t libisyntax_get_tile_width(const isyntax_t* isyntax) {
    return isyntax->tile_width;
}

int32_t libisyntax_get_tile_height(const isyntax_t* isyntax) {
    return isyntax->tile_height;
}

const isyntax_image_t* libisyntax_get_wsi_image(const isyntax_t* isyntax) {
    return isyntax->images + isyntax->wsi_image_index;
}

int32_t libisyntax_get_is_mpp_known(const isyntax_t* isyntax) {
    return isyntax->is_mpp_known;
}

double libisyntax_get_mpp_x(const isyntax_t* isyntax) {
    return isyntax->mpp_x;
}

double libisyntax_get_mpp_y(const isyntax_t* isyntax) {
    return isyntax->mpp_y;
}

const isyntax_image_t* libisyntax_get_label_image(const isyntax_t* isyntax) {
	return isyntax->images + isyntax->label_image_index;
}

const isyntax_image_t* libisyntax_get_macro_image(const isyntax_t* isyntax) {
	return isyntax->images + isyntax->macro_image_index;
}

const char* libisyntax_get_barcode(const isyntax_t* isyntax) {
	return isyntax->barcode;
}

const char* libisyntax_get_acquisition_datetime(const isyntax_t* isyntax) {
	return isyntax->dicom_acquisition_datetime;
}

const char* libisyntax_get_manufacturer(const isyntax_t* isyntax) {
	return isyntax->dicom_manufacturer;
}

const char* libisyntax_get_manufacturers_model_name(const isyntax_t* isyntax) {
    return isyntax->dicom_manufacturers_model_name;
}

const char* libisyntax_scale_unit(const isyntax_t* isyntax) {
    return isyntax->image_dimension_unit;
}
const char* libisyntax_get_derivation_description(const isyntax_t* isyntax) {
    return isyntax->dicom_derivation_description;
}

const char* libisyntax_get_device_serial_number(const isyntax_t* isyntax) {
    return isyntax->dicom_device_serial_number;
}

int32_t libisyntax_get_software_versions_count(const isyntax_t* isyntax) {
    return isyntax->dicom_software_versions_count;
}

const char* libisyntax_get_software_versions(const isyntax_t* isyntax, int32_t index) {
    if (index < 0 || index >= isyntax->dicom_software_versions_count) {
        return NULL;
    }
    return isyntax->dicom_software_versions[index];
}

int32_t libisyntax_get_date_of_last_calibration_count(const isyntax_t* isyntax) {
    return isyntax->dicom_date_of_last_calibration_count;
}

const char* libisyntax_get_date_of_last_calibration(const isyntax_t* isyntax, int32_t index) {
    if (index < 0 || index >= isyntax->dicom_date_of_last_calibration_count) {
        return NULL;
    }
    return isyntax->dicom_date_of_last_calibration[index];
}

int32_t libisyntax_get_time_of_last_calibration_count(const isyntax_t* isyntax) {
    return isyntax->dicom_time_of_last_calibration_count;
}

const char* libisyntax_get_time_of_last_calibration(const isyntax_t* isyntax, int32_t index) {
    if (index < 0 || index >= isyntax->dicom_time_of_last_calibration_count) {
        return NULL;
    }
    return isyntax->dicom_time_of_last_calibration[index];
}

bool libisyntax_is_lossy_image_compression(const isyntax_t* isyntax) {
    return isyntax->dicom_lossy_image_compression;
}

double libisyntax_get_lossy_image_compression_ratio(const isyntax_t* isyntax) {
    return isyntax->dicom_lossy_image_compression_ratio;
}

const char* libisyntax_get_lossy_image_compression_method(const isyntax_t* isyntax) {
    return isyntax->dicom_lossy_image_compression_method;
}

int32_t libisyntax_get_data_model_major_version(const isyntax_t* isyntax) {
	return isyntax->data_model_major_version;
}

int32_t libisyntax_get_data_model_minor_version(const isyntax_t* isyntax) {
	return isyntax->data_model_minor_version;
}

int32_t libisyntax_image_get_level_count(const isyntax_image_t* image) {
    return image->level_count;
}

int32_t libisyntax_image_get_offset_x(const isyntax_image_t* image) {
    return image->offset_x;
}

int32_t libisyntax_image_get_offset_y(const isyntax_image_t* image) {
    return image->offset_y;
}

const isyntax_level_t* libisyntax_image_get_level(const isyntax_image_t* image, int32_t index) {
    return &image->levels[index];
}

int32_t libisyntax_level_get_scale(const isyntax_level_t* level) {
    return level->scale;
}

int32_t libisyntax_level_get_width_in_tiles(const isyntax_level_t* level) {
    return level->width_in_tiles;
}

int32_t libisyntax_level_get_height_in_tiles(const isyntax_level_t* level) {
    return level->height_in_tiles;
}

int32_t libisyntax_level_get_width(const isyntax_level_t* level) {
	return level->width;
}

int32_t libisyntax_level_get_height(const isyntax_level_t* level) {
	return level->height;
}

float libisyntax_level_get_mpp_x(const isyntax_level_t* level) {
	return level->um_per_pixel_x;
}

float libisyntax_level_get_mpp_y(const isyntax_level_t* level) {
	return level->um_per_pixel_y;
}

double libisyntax_level_get_downsample_factor(const isyntax_level_t* level) {
    return level->downsample_factor;
}

double libisyntax_level_get_origin_offset_in_pixels(const isyntax_level_t* level) {
    return level->origin_offset_in_pixels;
}

isyntax_error_t libisyntax_cache_create(const char* debug_name_or_null, int32_t cache_size,
                                        isyntax_cache_t** out_isyntax_cache)
{
    isyntax_cache_t* cache_ptr = malloc(sizeof(isyntax_cache_t));
    memset(cache_ptr, 0, sizeof(*cache_ptr));
    tile_list_init(&cache_ptr->cache_list, debug_name_or_null);
    cache_ptr->target_cache_size = cache_size;
    platform_mutex_init(&cache_ptr->mutex);

    // Note: rest of initialization is deferred to the first injection, as that is where we will know the block size.

    *out_isyntax_cache = cache_ptr;
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_cache_inject(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax) {
    // TODO(avirodov): consider refactoring implementation to another file, here and in destroy.
    if (isyntax->ll_coeff_block_allocator != NULL || isyntax->h_coeff_block_allocator != NULL) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }

    isyntax_cache->allocator_block_width = isyntax->block_width;
    isyntax_cache->allocator_block_height = isyntax->block_height;
    size_t ll_coeff_block_size = isyntax->block_width * isyntax->block_height * sizeof(icoeff_t);
    size_t block_allocator_maximum_capacity_in_blocks = GIGABYTES(32) / ll_coeff_block_size;
    size_t ll_coeff_block_allocator_capacity_in_blocks = block_allocator_maximum_capacity_in_blocks / 4;
    size_t h_coeff_block_size = ll_coeff_block_size * 3;
    size_t h_coeff_block_allocator_capacity_in_blocks = ll_coeff_block_allocator_capacity_in_blocks * 3;
    isyntax_cache->ll_coeff_block_allocator = malloc(sizeof(block_allocator_t));
    isyntax_cache->h_coeff_block_allocator = malloc(sizeof(block_allocator_t));
    block_allocator_init(isyntax_cache->ll_coeff_block_allocator, ll_coeff_block_size, ll_coeff_block_allocator_capacity_in_blocks, MEGABYTES(256));
    block_allocator_init(isyntax_cache->h_coeff_block_allocator, h_coeff_block_size, h_coeff_block_allocator_capacity_in_blocks, MEGABYTES(256));
    isyntax_cache->is_block_allocator_owned = true;

    if (isyntax_cache->allocator_block_width != isyntax->block_width ||
            isyntax_cache->allocator_block_height != isyntax->block_height) {
        return LIBISYNTAX_FATAL; // Not implemented, see todo in libisyntax.h.
    }

    isyntax->ll_coeff_block_allocator = isyntax_cache->ll_coeff_block_allocator;
    isyntax->h_coeff_block_allocator = isyntax_cache->h_coeff_block_allocator;
    isyntax->is_block_allocator_owned = false;
    return LIBISYNTAX_OK;
}


void libisyntax_cache_flush(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax_or_null) {
    // Flusing per-isyntax is not yet implemented.
    (void)isyntax_or_null;

    platform_mutex_lock(&isyntax_cache->mutex);

    while (isyntax_cache->cache_list.tail) {
        tile_list_remove(&isyntax_cache->cache_list, isyntax_cache->cache_list.tail);
    }

    platform_mutex_unlock(&isyntax_cache->mutex);
}

void libisyntax_cache_destroy(isyntax_cache_t* isyntax_cache) {
    if (isyntax_cache->is_block_allocator_owned) {
        if (isyntax_cache->ll_coeff_block_allocator->is_valid) {
            block_allocator_destroy(isyntax_cache->ll_coeff_block_allocator);
        }
        if (isyntax_cache->h_coeff_block_allocator->is_valid) {
            block_allocator_destroy(isyntax_cache->h_coeff_block_allocator);
        }
    }

    platform_mutex_destroy(&isyntax_cache->mutex);
    free(isyntax_cache);
}

// TODO(pvalkema): should we allow passing a stride for the pixels_buffer, to allow blitting into buffers
//  that are not exactly the height/width of the region?
isyntax_error_t libisyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache,
                                     int32_t level, int64_t tile_x, int64_t tile_y,
                                     uint32_t* pixels_buffer, int32_t pixel_format) {
    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }
    // TODO(avirodov): additional vaidations, e.g. tile_x >= 0 && tile_x < isyntax...[level]...->width_in_tiles.

    // TODO(avirodov): if isyntax_cache is null, we can support using allocators that are in isyntax object,
    //  if is_init_allocators = 1 when created. Not sure is needed.
    isyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y, pixels_buffer, pixel_format);
    return LIBISYNTAX_OK;
}

#define PER_LEVEL_PADDING 3

isyntax_error_t libisyntax_read_region(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache, int32_t level,
                                       int64_t x, int64_t y, int64_t width, int64_t height, uint32_t* pixels_buffer,
                                       int32_t pixel_format) {

    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }

    // Get the level
    ASSERT(level < isyntax->images[0].level_count);
    isyntax_level_t* current_level = &isyntax->images[0].levels[level];

    // TODO(pvalkema): check if this still needs adjustment
    int32_t num_levels = isyntax->images[0].level_count;
    int32_t offset = ((PER_LEVEL_PADDING << num_levels) - PER_LEVEL_PADDING) >> level;

    x += offset;
    y += offset;

    int32_t tile_width = isyntax->tile_width;
    int32_t tile_height = isyntax->tile_height;

    int64_t start_tile_x;
    int64_t end_tile_x;
    int64_t x_remainder;
    int64_t x_remainder_last;

    if (x > 0) {
        start_tile_x = x / tile_width;
        end_tile_x = (x + width - 1) / tile_width;
        x_remainder = x % tile_width;
        x_remainder_last = (x + width - 1) % tile_width;
    } else {
        start_tile_x = -(-x / tile_width);
        end_tile_x = -(-(x + width - 1) / tile_width);
        x_remainder = (x % tile_width + tile_width) % tile_width;
        x_remainder_last = ((x + width - 1) % tile_width + tile_width) % tile_width;
    }

    int64_t start_tile_y;
    int64_t end_tile_y;
    int64_t y_remainder;
    int64_t y_remainder_last;

    if (y > 0) {
        start_tile_y = y / tile_height;
        end_tile_y = (y + height - 1) / tile_height;
        y_remainder = y % tile_height;
        y_remainder_last = (y + height - 1) % tile_height;
    } else {
        start_tile_y = -(-y / tile_height);
        end_tile_y = -(-(y + height - 1) / tile_height);
        y_remainder = (y % tile_height + tile_height) % tile_height;
        y_remainder_last = ((y + height - 1) % tile_height + tile_height) % tile_height;
    }

    // Allocate memory for tile pixels (will reuse for consecutive libisyntax_tile_read() calls)
    uint32_t* tile_pixels = (uint32_t*)malloc(tile_width * tile_height * sizeof(uint32_t));

    // Read tiles and copy the relevant portion of each tile to the region
    for (int64_t tile_y = start_tile_y; tile_y <= end_tile_y; ++tile_y) {
        for (int64_t tile_x = start_tile_x; tile_x <= end_tile_x; ++tile_x) {
            // Calculate the portion of the tile to be copied
            int64_t src_x = (tile_x == start_tile_x) ? x_remainder : 0;
            int64_t src_y = (tile_y == start_tile_y) ? y_remainder : 0;
            int64_t dest_x = (tile_x == start_tile_x) ? 0 : (tile_x - start_tile_x) * tile_width - x_remainder;
            int64_t dest_y = (tile_y == start_tile_y) ? 0 : (tile_y - start_tile_y) * tile_height - y_remainder;
            int64_t copy_width = (tile_x == end_tile_x) ? x_remainder_last - src_x + 1 : tile_width - src_x;
            int64_t copy_height = (tile_y == end_tile_y) ? y_remainder_last - src_y + 1 : tile_height - src_y;

            // Read tile
            CHECK_LIBISYNTAX_OK(libisyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y, tile_pixels, pixel_format));

            // Copy the relevant portion of the tile to the region
            for (int64_t i = 0; i < copy_height; ++i) {
                int64_t dest_index = (dest_y + i) * width + dest_x;
                int64_t src_index = (src_y + i) * tile_width + src_x;
                memcpy((pixels_buffer) + dest_index,
                       tile_pixels + src_index,
                       copy_width * sizeof(uint32_t));
            }
        }
    }

    free(tile_pixels);

    return LIBISYNTAX_OK;
}

// TODO(pvalkema): remove this / only support returning compressed JPEG buffer and leave decompression to caller?
static isyntax_error_t libisyntax_read_associated_image(isyntax_t* isyntax, isyntax_image_t* image, int32_t* width, int32_t* height,
                                                        uint32_t** pixels_buffer, int32_t pixel_format) {
    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }
    uint32_t* pixels = (uint32_t*)isyntax_get_associated_image_pixels(isyntax, image, pixel_format);
    // NOTE: the width and height are only known AFTER the decoding.
    if (width) *width = image->width;
    if (height) *height = image->height;
    if (pixels_buffer) *pixels_buffer = pixels;
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_read_label_image(isyntax_t* isyntax, int32_t* width, int32_t* height,
                                            uint32_t** pixels_buffer, int32_t pixel_format) {
    isyntax_image_t* label_image = isyntax->images + isyntax->label_image_index;
    return libisyntax_read_associated_image(isyntax, label_image, width, height, pixels_buffer, pixel_format);
}

isyntax_error_t libisyntax_read_macro_image(isyntax_t* isyntax, int32_t* width, int32_t* height,
                                            uint32_t** pixels_buffer, int32_t pixel_format) {
    isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
    return libisyntax_read_associated_image(isyntax, macro_image, width, height, pixels_buffer, pixel_format);
}

static isyntax_error_t libisyntax_read_assocatiated_image_jpeg(isyntax_t* isyntax, isyntax_image_t* image, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    ASSERT(jpeg_buffer);
    ASSERT(jpeg_size);
    u8* jpeg_compressed = isyntax_get_associated_image_jpeg(isyntax, image, jpeg_size);
    if (jpeg_compressed) {
        *jpeg_buffer = jpeg_compressed;
        return LIBISYNTAX_OK;
    } else {
        return LIBISYNTAX_FATAL;
    }
}

isyntax_error_t libisyntax_read_label_image_jpeg(isyntax_t* isyntax, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    isyntax_image_t* label_image = isyntax->images + isyntax->label_image_index;
    return libisyntax_read_assocatiated_image_jpeg(isyntax, label_image, jpeg_buffer, jpeg_size);
}

isyntax_error_t libisyntax_read_macro_image_jpeg(isyntax_t* isyntax, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
    return libisyntax_read_assocatiated_image_jpeg(isyntax, macro_image, jpeg_buffer, jpeg_size);
}

isyntax_error_t libisyntax_read_icc_profile(isyntax_t* isyntax, isyntax_image_t* image, uint8_t** icc_profile_buffer, uint32_t* icc_profile_size) {
    ASSERT(icc_profile_buffer);
    ASSERT(icc_profile_size);
    u8* icc_profile_compressed = isyntax_get_icc_profile(isyntax, image, icc_profile_size);
    if (icc_profile_compressed) {
        *icc_profile_buffer = icc_profile_compressed;
        return LIBISYNTAX_OK;
    } else {
        return LIBISYNTAX_FATAL;
    }
}

void libisyntax_image_set_postprocessing(isyntax_image_t* image, int32_t flags) {
    image->postprocessing_flags = flags;
}

isyntax_error_t libisyntax_image_prepare_postprocessing(isyntax_t* isyntax,
                                                        isyntax_cache_t* cache,
                                                        isyntax_image_t* image) {
    if (!(image->postprocessing_flags & LIBISYNTAX_POSTPROCESSING_CLAHE)) return LIBISYNTAX_OK;
    if (image->clahe_nr_bins <= 0 || image->clahe_context_dim <= 0) return LIBISYNTAX_OK;
    if (image->image_type != ISYNTAX_IMAGE_TYPE_WSI) return LIBISYNTAX_INVALID_ARGUMENT;

    if (image->clahe_lut_grid) {
        free(image->clahe_lut_grid);
        image->clahe_lut_grid = NULL;
    }

    isyntax_level_t* coarse = image->levels + image->max_scale;
    i32 tile_width  = isyntax->tile_width;
    i32 tile_height = isyntax->tile_height;
    i32 luma_w = coarse->width_in_tiles  * tile_width;
    i32 luma_h = coarse->height_in_tiles * tile_height;

    u8* luma_buf = (u8*)malloc((size_t)luma_w * luma_h);
    if (!luma_buf) return LIBISYNTAX_FATAL;

    u32* tile_rgba = (u32*)malloc((size_t)tile_width * tile_height * sizeof(u32));
    if (!tile_rgba) { free(luma_buf); return LIBISYNTAX_FATAL; }

    for (i32 ty = 0; ty < coarse->height_in_tiles; ++ty) {
        for (i32 tx = 0; tx < coarse->width_in_tiles; ++tx) {
            isyntax_tile_read(isyntax, cache, image->max_scale, tx, ty,
                              tile_rgba, LIBISYNTAX_PIXEL_FORMAT_RGBA);
            u8* dest_row = luma_buf + ty * tile_height * luma_w + tx * tile_width;
            for (i32 py = 0; py < tile_height; ++py) {
                const u32* src = tile_rgba + py * tile_width;
                u8* dst = dest_row + py * luma_w;
                for (i32 px = 0; px < tile_width; ++px) {
                    u32 rgba = src[px];
                    u32 r = (rgba >>  0) & 0xFF;
                    u32 g = (rgba >>  8) & 0xFF;
                    u32 b = (rgba >> 16) & 0xFF;
                    dst[px] = (u8)((r + g + g + b) >> 2);
                }
            }
        }
    }
    free(tile_rgba);

    i32 ctx  = image->clahe_context_dim;
    i32 bins = image->clahe_nr_bins;
    i32 gw   = (luma_w + ctx - 1) / ctx;
    i32 gh   = (luma_h + ctx - 1) / ctx;

    u8* grid = (u8*)malloc((size_t)gw * gh * bins);
    if (!grid) { free(luma_buf); return LIBISYNTAX_FATAL; }

    u32* hist = (u32*)malloc((size_t)bins * sizeof(u32));
    if (!hist) { free(grid); free(luma_buf); return LIBISYNTAX_FATAL; }

    float clip_count = (image->clahe_clip_limit > 0.0f) ? image->clahe_clip_limit : 40.0f;

    for (i32 gy = 0; gy < gh; ++gy) {
        for (i32 gx = 0; gx < gw; ++gx) {
            i32 x0 = gx * ctx;
            i32 y0 = gy * ctx;
            i32 x1 = x0 + ctx; if (x1 > luma_w) x1 = luma_w;
            i32 y1 = y0 + ctx; if (y1 > luma_h) y1 = luma_h;
            i32 n  = (x1 - x0) * (y1 - y0);

            memset(hist, 0, (size_t)bins * sizeof(u32));
            for (i32 py = y0; py < y1; ++py) {
                const u8* row = luma_buf + py * luma_w;
                for (i32 px = x0; px < x1; ++px) {
                    u32 bin = (u32)row[px] * (u32)bins >> 8;
                    if (bin >= (u32)bins) bin = (u32)bins - 1;
                    ++hist[bin];
                }
            }

            u32 clip = (u32)(clip_count * (float)n / (float)bins);
            if (clip < 1) clip = 1;
            u32 excess = 0;
            for (i32 b = 0; b < bins; ++b) {
                if (hist[b] > clip) { excess += hist[b] - clip; hist[b] = clip; }
            }
            u32 redist = excess / (u32)bins;
            u32 leftover = excess - redist * (u32)bins;
            for (i32 b = 0; b < bins; ++b) {
                hist[b] += redist;
                if ((u32)b < leftover) ++hist[b];
            }

            u8* lut = grid + ((size_t)gy * gw + gx) * bins;
            u32 cdf = 0;
            for (i32 b = 0; b < bins; ++b) {
                cdf += hist[b];
                u32 mapped = (cdf * 255u + (u32)n / 2) / (u32)n;
                lut[b] = (u8)(mapped < 255u ? mapped : 255u);
            }
        }
    }
    free(hist);
    free(luma_buf);

    image->clahe_lut_grid    = grid;
    image->clahe_grid_width  = gw;
    image->clahe_grid_height = gh;
    image->clahe_luma_width  = luma_w;
    image->clahe_luma_height = luma_h;
    return LIBISYNTAX_OK;
}
