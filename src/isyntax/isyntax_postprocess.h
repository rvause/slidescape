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

#pragma once

#include "common.h"

// Display post-processing for decoded iSyntax tiles.
// Operates in-place on BGRA tiles (byte order B, G, R, A) as produced by
// the iSyntax decoder with LIBISYNTAX_PIXEL_FORMAT_BGRA. Three optional
// stages applied in order: CLAHE -> sharpness -> contrast. A stage runs only
// when its apply_* flag is set; with all flags clear isyntax_pp_apply_bgra
// is a no-op.
//
// Expensive, run-fixed data (CLAHE grid + contrast LUT) is precomputed once by
// isyntax_pp_setup_create from the coarsest pyramid level and then shared
// read-only across all tile decode calls, so every tile at every zoom level
// sees the same globally-consistent tone map.

typedef struct {
	bool  apply_clahe;
	float clahe_clip_limit;    // >1 limits amplification; default 1.2 (Philips UFS)
	i32   clahe_context_dim;   // context region size in coarse-level pixels; default 40

	bool  apply_sharpness;
	float sharpness_gain;      // default 2.0; range [0, 10]

	bool  apply_contrast;
	float contrast;            // default 1.2; out = clamp(128 + contrast*(in - 128))
} isyntax_pp_params_t;

typedef struct isyntax_pp_setup_t isyntax_pp_setup_t;

// Populate p with Philips UFS defaults. All stages disabled (apply_* = false).
void isyntax_pp_params_defaults(isyntax_pp_params_t* p);

// Build precomputed setup (CLAHE grid + contrast LUT) from the coarsest pyramid
// level. coarse_bgra is a stitched BGRA buffer of coarse_w x coarse_h pixels.
// coarse_downsample is the full-resolution downsample factor of that level
// (2^max_scale). Returns NULL on allocation failure.
// coarse_bgra may be NULL when apply_clahe is false; CLAHE will be skipped.
isyntax_pp_setup_t* isyntax_pp_setup_create(const isyntax_pp_params_t* p,
	const u32* coarse_bgra, i32 coarse_w, i32 coarse_h, float coarse_downsample);

void isyntax_pp_setup_destroy(isyntax_pp_setup_t* s);

// Apply enabled stages in-place over width*height BGRA pixels.
//
// region_downsample: 2^scale — full-resolution pixels per decoded pixel.
// region_x, region_y: tile origin in full-resolution pixel coordinates.
// These are only used when CLAHE is active; ignored otherwise.
void isyntax_pp_apply_bgra(u32* bgra, i32 width, i32 height,
	const isyntax_pp_params_t* p, const isyntax_pp_setup_t* s,
	float region_downsample, i32 region_x, i32 region_y);
