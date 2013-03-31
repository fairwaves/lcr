/* HR (GSM 06.20) codec wrapper */

/*
 * This file is part of gapk (GSM Audio Pocket Knife).
 *
 * gapk is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gapk is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gapk.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsmhr/gsmhr.h>

#include "refsrc/typedefs.h"
#include "refsrc/homing.h"
#include "refsrc/sp_dec.h"
#include "refsrc/sp_enc.h"


#define EXPORT __attribute__((visibility("default")))


struct gsmhr {
	int dec_reset_flg;
};

EXPORT struct gsmhr *
gsmhr_init(void)
{
	struct gsmhr *state;

	state = calloc(1, sizeof(struct gsmhr));
	if (!state)
		return NULL;

	state->dec_reset_flg = 1;

	return state;
}

EXPORT void
gsmhr_exit(struct gsmhr *state)
{
	free(state);
}

EXPORT int
gsmhr_encode(struct gsmhr *state, int16_t *hr_params, const int16_t *pcm)
{
	int enc_reset_flg;
	Shortword pcm_b[F_LEN];

	memcpy(pcm_b, pcm, F_LEN*sizeof(int16_t));

	enc_reset_flg = encoderHomingFrameTest(pcm_b);

	speechEncoder(pcm_b, hr_params);

	if (enc_reset_flg)
		resetEnc();

	return 0;
}

EXPORT int
gsmhr_decode(struct gsmhr *state, int16_t *pcm, const int16_t *hr_params)
{
#define WHOLE_FRAME		18
#define TO_FIRST_SUBFRAME	 9

	int dec_reset_flg;
	Shortword hr_params_b[22];

	memcpy(hr_params_b, hr_params, 22*sizeof(int16_t));

	if (state->dec_reset_flg)
		dec_reset_flg = decoderHomingFrameTest(hr_params_b, TO_FIRST_SUBFRAME);
	else
		dec_reset_flg = 0;

	if (dec_reset_flg && state->dec_reset_flg) {
		int i;
		for (i=0; i<F_LEN; i++)
			pcm[i] = EHF_MASK;
	} else {
		speechDecoder(hr_params_b, pcm);
	}

	if (!state->dec_reset_flg)
		dec_reset_flg = decoderHomingFrameTest(hr_params_b, WHOLE_FRAME);

	if (dec_reset_flg)
		resetDec();

	state->dec_reset_flg = dec_reset_flg;

	return 0;
}
