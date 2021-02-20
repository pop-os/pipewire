/* Spa
 *
 * Copyright © 2021 Wim Taymans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "volume-ops.h"

#include <xmmintrin.h>

void
volume_f32_sse(struct volume *vol, void * SPA_RESTRICT dst,
		const void * SPA_RESTRICT src, float volume, uint32_t n_samples)
{
	uint32_t n, unrolled;
	float *d = (float*)dst;
	const float *s = (const float*)src;

	if (volume == VOLUME_MIN) {
		memset(d, 0, n_samples * sizeof(float));
	}
	else if (volume == VOLUME_NORM) {
		spa_memcpy(d, s, n_samples * sizeof(float));
	}
	else {
		__m128 t[4];
		const __m128 vol = _mm_set1_ps(volume);

		if (SPA_IS_ALIGNED(d, 16) &&
		    SPA_IS_ALIGNED(s, 16))
			unrolled = n_samples & ~15;
		else
			unrolled = 0;

		for(n = 0; n < unrolled; n += 16) {
			t[0] = _mm_load_ps(&s[n]);
			t[1] = _mm_load_ps(&s[n+4]);
			t[2] = _mm_load_ps(&s[n+8]);
			t[3] = _mm_load_ps(&s[n+12]);
			_mm_store_ps(&d[n], _mm_mul_ps(t[0], vol));
			_mm_store_ps(&d[n+4], _mm_mul_ps(t[1], vol));
			_mm_store_ps(&d[n+8], _mm_mul_ps(t[2], vol));
			_mm_store_ps(&d[n+12], _mm_mul_ps(t[3], vol));
		}
		for(; n < n_samples; n++)
			_mm_store_ss(&d[n], _mm_mul_ss(_mm_load_ss(&s[n]), vol));
	}
}
