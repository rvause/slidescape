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

#include "isyntax_postprocess.h"
#include <stdlib.h>
#include <string.h>

struct isyntax_pp_setup_t {
	u8    contrast_lut[256];
	u8*   clahe_grid;        // grid_w * grid_h * 256 bytes; [gy*grid_w+gx][luma] -> remapped luma
	i32   grid_w;
	i32   grid_h;
	i32   context_dim;
	float coarse_downsample;
};

void isyntax_pp_params_defaults(isyntax_pp_params_t* p) {
	p->apply_clahe      = false;
	p->clahe_clip_limit = 1.2f;
	p->clahe_context_dim = 40;
	p->apply_sharpness  = false;
	p->sharpness_gain   = 2.0f;
	p->apply_contrast   = false;
	p->contrast         = 1.2f;
}

static u8 clamp_u8(i32 v) {
	return (u8)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// BT.601 luma from a BGRA pixel (B=byte0, G=byte1, R=byte2)
static u8 pixel_luma_bgra(u32 bgra) {
	u32 b = bgra & 0xFF;
	u32 g = (bgra >> 8) & 0xFF;
	u32 r = (bgra >> 16) & 0xFF;
	return (u8)((29u * b + 150u * g + 77u * r) >> 8);
}

static void build_contrast_lut(u8* lut, float contrast) {
	for (i32 i = 0; i < 256; ++i) {
		lut[i] = clamp_u8((i32)(128.0f + contrast * (float)(i - 128) + 0.5f));
	}
}

// Zuiderveld CLAHE: build one 256-entry LUT for a single context region.
static void build_clahe_region_lut(const u32* coarse_bgra, i32 cw,
	i32 x0, i32 y0, i32 x1, i32 y1, float clip_limit, u8* out_lut) {
	i32 region_pixels = (x1 - x0) * (y1 - y0);
	i32 hist[256];
	memset(hist, 0, sizeof(hist));

	for (i32 py = y0; py < y1; ++py) {
		for (i32 px = x0; px < x1; ++px) {
			hist[pixel_luma_bgra(coarse_bgra[py * cw + px])]++;
		}
	}

	// Clip and redistribute excess uniformly
	i32 clip = (i32)(clip_limit * (float)region_pixels / 256.0f + 0.5f);
	if (clip < 1) clip = 1;
	i32 excess = 0;
	for (i32 i = 0; i < 256; ++i) {
		if (hist[i] > clip) {
			excess += hist[i] - clip;
			hist[i] = clip;
		}
	}
	i32 bonus   = excess / 256;
	i32 leftover = excess - bonus * 256;
	for (i32 i = 0; i < 256; ++i) hist[i] += bonus;
	for (i32 i = 0; i < leftover; ++i) hist[i]++;

	// Cumulative distribution -> LUT
	i32 cum = 0;
	for (i32 i = 0; i < 256; ++i) {
		cum += hist[i];
		out_lut[i] = (u8)((cum * 255 + region_pixels / 2) / region_pixels);
	}
}

static u8* build_clahe_grid(const u32* coarse_bgra, i32 cw, i32 ch,
	float clip_limit, i32 context_dim, i32* out_grid_w, i32* out_grid_h) {
	i32 grid_w = (cw + context_dim - 1) / context_dim;
	i32 grid_h = (ch + context_dim - 1) / context_dim;
	u8* grid = (u8*)malloc((size_t)grid_w * (size_t)grid_h * 256u);
	if (!grid) return NULL;

	for (i32 gy = 0; gy < grid_h; ++gy) {
		for (i32 gx = 0; gx < grid_w; ++gx) {
			i32 x0 = gx * context_dim;
			i32 y0 = gy * context_dim;
			i32 x1 = x0 + context_dim < cw ? x0 + context_dim : cw;
			i32 y1 = y0 + context_dim < ch ? y0 + context_dim : ch;
			build_clahe_region_lut(coarse_bgra, cw, x0, y0, x1, y1, clip_limit,
				grid + ((size_t)gy * grid_w + gx) * 256u);
		}
	}

	*out_grid_w = grid_w;
	*out_grid_h = grid_h;
	return grid;
}

isyntax_pp_setup_t* isyntax_pp_setup_create(const isyntax_pp_params_t* p,
	const u32* coarse_bgra, i32 coarse_w, i32 coarse_h, float coarse_downsample) {
	isyntax_pp_setup_t* s = (isyntax_pp_setup_t*)calloc(1, sizeof(*s));
	if (!s) return NULL;

	build_contrast_lut(s->contrast_lut, p->apply_contrast ? p->contrast : 1.0f);
	s->coarse_downsample = coarse_downsample;
	s->context_dim = p->clahe_context_dim;

	if (p->apply_clahe && coarse_bgra && coarse_w > 0 && coarse_h > 0) {
		s->clahe_grid = build_clahe_grid(coarse_bgra, coarse_w, coarse_h,
			p->clahe_clip_limit, p->clahe_context_dim, &s->grid_w, &s->grid_h);
	}
	return s;
}

void isyntax_pp_setup_destroy(isyntax_pp_setup_t* s) {
	if (!s) return;
	free(s->clahe_grid);
	free(s);
}

// --------------------------------------------------------------------------
// CLAHE apply
// --------------------------------------------------------------------------

static void apply_clahe(u32* bgra, i32 w, i32 h, const isyntax_pp_setup_t* s,
	float region_downsample, i32 region_x, i32 region_y) {
	if (!s->clahe_grid) return;
	float inv_coarse = 1.0f / s->coarse_downsample;
	float ctx_f      = (float)s->context_dim;
	i32 grid_w = s->grid_w;
	i32 grid_h = s->grid_h;

	for (i32 py = 0; py < h; ++py) {
		for (i32 px = 0; px < w; ++px) {
			// Position in coarse-level pixels
			float cx = ((float)region_x + (float)px * region_downsample) * inv_coarse;
			float cy = ((float)region_y + (float)py * region_downsample) * inv_coarse;

			// Grid fractional position (bilinear between cell centres)
			float fgx = cx / ctx_f - 0.5f;
			float fgy = cy / ctx_f - 0.5f;
			i32 igx = (i32)fgx;
			i32 igy = (i32)fgy;
			float tfx = fgx - (float)igx;
			float tfy = fgy - (float)igy;
			if (tfx < 0.0f) { tfx = 0.0f; }
			if (tfy < 0.0f) { tfy = 0.0f; }

			// Clamp cell indices
			i32 gx0 = igx     < 0 ? 0 : (igx     >= grid_w ? grid_w - 1 : igx);
			i32 gx1 = igx + 1 < 0 ? 0 : (igx + 1 >= grid_w ? grid_w - 1 : igx + 1);
			i32 gy0 = igy     < 0 ? 0 : (igy     >= grid_h ? grid_h - 1 : igy);
			i32 gy1 = igy + 1 < 0 ? 0 : (igy + 1 >= grid_h ? grid_h - 1 : igy + 1);

			u32 pix = bgra[py * w + px];
			u32 b = pix & 0xFF;
			u32 g = (pix >> 8) & 0xFF;
			u32 r = (pix >> 16) & 0xFF;
			u32 a = pix >> 24;
			u8 y_in = (u8)((29u * b + 150u * g + 77u * r) >> 8);

			// Bilinear blend of four surrounding LUT entries at y_in
			const u8* l00 = s->clahe_grid + ((size_t)gy0 * grid_w + gx0) * 256u;
			const u8* l10 = s->clahe_grid + ((size_t)gy0 * grid_w + gx1) * 256u;
			const u8* l01 = s->clahe_grid + ((size_t)gy1 * grid_w + gx0) * 256u;
			const u8* l11 = s->clahe_grid + ((size_t)gy1 * grid_w + gx1) * 256u;
			float v = (float)l00[y_in] * (1.0f - tfx) * (1.0f - tfy)
			        + (float)l10[y_in] * tfx           * (1.0f - tfy)
			        + (float)l01[y_in] * (1.0f - tfx)  * tfy
			        + (float)l11[y_in] * tfx            * tfy;

			// Scale RGB to new luma while preserving hue
			if (y_in > 0) {
				float scale = v / (float)y_in;
				b = (u32)clamp_u8((i32)((float)b * scale + 0.5f));
				g = (u32)clamp_u8((i32)((float)g * scale + 0.5f));
				r = (u32)clamp_u8((i32)((float)r * scale + 0.5f));
			} else {
				u32 ny = (u32)(v + 0.5f);
				b = ny; g = ny; r = ny;
			}
			bgra[py * w + px] = b | (g << 8) | (r << 16) | (a << 24);
		}
	}
}

// --------------------------------------------------------------------------
// Sharpness (unsharp mask): 5-tap [1 4 6 4 1]/16 separable Gaussian blur
// --------------------------------------------------------------------------

// Horizontal pass
static void blur5_h(const u32* src, u32* dst, i32 w, i32 h) {
	for (i32 y = 0; y < h; ++y) {
		const u32* row = src + (size_t)y * w;
		u32*        out = dst + (size_t)y * w;
		for (i32 x = 0; x < w; ++x) {
			i32 xm2 = x > 1     ? x - 2 : 0;
			i32 xm1 = x > 0     ? x - 1 : 0;
			i32 xp1 = x < w - 1 ? x + 1 : w - 1;
			i32 xp2 = x < w - 2 ? x + 2 : w - 1;
			u32 res = 0;
			for (i32 shift = 0; shift < 24; shift += 8) {
				i32 sum = (i32)((row[xm2] >> shift) & 0xFF)
				        + ((i32)((row[xm1] >> shift) & 0xFF) * 4)
				        + ((i32)((row[x]   >> shift) & 0xFF) * 6)
				        + ((i32)((row[xp1] >> shift) & 0xFF) * 4)
				        + (i32)((row[xp2] >> shift) & 0xFF);
				res |= (u32)((sum + 8) >> 4) << shift;
			}
			out[x] = res | (row[x] & 0xFF000000u);
		}
	}
}

// Vertical pass
static void blur5_v(const u32* src, u32* dst, i32 w, i32 h) {
	for (i32 y = 0; y < h; ++y) {
		i32 ym2 = y > 1     ? y - 2 : 0;
		i32 ym1 = y > 0     ? y - 1 : 0;
		i32 yp1 = y < h - 1 ? y + 1 : h - 1;
		i32 yp2 = y < h - 2 ? y + 2 : h - 1;
		const u32* r0 = src + (size_t)ym2 * w;
		const u32* r1 = src + (size_t)ym1 * w;
		const u32* r2 = src + (size_t)y   * w;
		const u32* r3 = src + (size_t)yp1 * w;
		const u32* r4 = src + (size_t)yp2 * w;
		u32* out = dst + (size_t)y * w;
		for (i32 x = 0; x < w; ++x) {
			u32 res = 0;
			for (i32 shift = 0; shift < 24; shift += 8) {
				i32 sum = (i32)((r0[x] >> shift) & 0xFF)
				        + ((i32)((r1[x] >> shift) & 0xFF) * 4)
				        + ((i32)((r2[x] >> shift) & 0xFF) * 6)
				        + ((i32)((r3[x] >> shift) & 0xFF) * 4)
				        + (i32)((r4[x] >> shift) & 0xFF);
				res |= (u32)((sum + 8) >> 4) << shift;
			}
			out[x] = res | (r2[x] & 0xFF000000u);
		}
	}
}

static void apply_sharpness(u32* bgra, i32 w, i32 h, float gain) {
	size_t n = (size_t)w * (size_t)h;
	u32* tmp  = (u32*)malloc(n * sizeof(u32));
	u32* blur = (u32*)malloc(n * sizeof(u32));
	if (!tmp || !blur) {
		free(tmp);
		free(blur);
		return;
	}
	blur5_h(bgra, tmp, w, h);
	blur5_v(tmp,  blur, w, h);
	free(tmp);

	i32 gain_fp = (i32)(gain * 256.0f + 0.5f);
	for (size_t i = 0; i < n; ++i) {
		u32 orig    = bgra[i];
		u32 blurred = blur[i];
		u32 result  = orig & 0xFF000000u;
		for (i32 shift = 0; shift < 24; shift += 8) {
			i32 o  = (i32)((orig    >> shift) & 0xFF);
			i32 bl = (i32)((blurred >> shift) & 0xFF);
			result |= (u32)clamp_u8(o + ((gain_fp * (o - bl)) >> 8)) << shift;
		}
		bgra[i] = result;
	}
	free(blur);
}

// --------------------------------------------------------------------------
// Global contrast (tone LUT)
// --------------------------------------------------------------------------

static void apply_contrast_lut(u32* bgra, size_t n, const u8* lut) {
	for (size_t i = 0; i < n; ++i) {
		u32 p = bgra[i];
		bgra[i] = (u32)lut[p & 0xFF]
		        | ((u32)lut[(p >> 8) & 0xFF] << 8)
		        | ((u32)lut[(p >> 16) & 0xFF] << 16)
		        | (p & 0xFF000000u);
	}
}

// --------------------------------------------------------------------------
// Public entry point
// --------------------------------------------------------------------------

void isyntax_pp_apply_bgra(u32* bgra, i32 width, i32 height,
	const isyntax_pp_params_t* p, const isyntax_pp_setup_t* s,
	float region_downsample, i32 region_x, i32 region_y) {
	if (!p || !s) return;
	if (p->apply_clahe && s->clahe_grid) {
		apply_clahe(bgra, width, height, s, region_downsample, region_x, region_y);
	}
	if (p->apply_sharpness && p->sharpness_gain > 0.0f) {
		apply_sharpness(bgra, width, height, p->sharpness_gain);
	}
	if (p->apply_contrast) {
		apply_contrast_lut(bgra, (size_t)width * (size_t)height, s->contrast_lut);
	}
}
