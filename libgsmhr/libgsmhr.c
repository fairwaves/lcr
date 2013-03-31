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

#define EHF_MASK 0x0008                /* Encoder Homing Frame pattern */
#define LMAX 142                       /* largest lag (integer sense) */
#define CG_INT_MACS 6                  /* Number of multiply-accumulates in
                                        * one interpolation           */
#define NUM_CLOSED 3                   /* maximum number of lags searched */
#define LPCSTARTINDEX 25               /* where the LPC analysis window
                                        * starts                        */
#define INBUFFSZ LPCSTARTINDEX + A_LEN /* input buffer size */
#define LTP_LEN         147            /* maximum ltp lag */
#define LSMAX           (LMAX + CG_INT_MACS/2)
#define HNW_BUFF_LEN    LSMAX

  extern Shortword swOldR0;
  extern Shortword swOldR0Index;

  extern struct NormSw psnsWSfrmEngSpace[];

  extern Shortword pswHPFXState[];
  extern Shortword pswHPFYState[];
  extern Shortword pswOldFrmKs[];
  extern Shortword pswOldFrmAs[];
  extern Shortword pswOldFrmSNWCoefs[];
  extern Shortword pswWgtSpeechSpace[];

  extern Shortword pswSpeech[];        /* input speech */

  extern Shortword swPtch;

  extern Shortword pswAnalysisState[NP];

  extern Shortword pswWStateNum[NP],
         pswWStateDenom[NP];


  extern Shortword pswLtpStateBase[LTP_LEN + S_LEN];
  extern Shortword pswHState[NP];
  extern Shortword pswHNWState[HNW_BUFF_LEN];
  extern Shortword gswPostFiltAgcGain,
         gpswPostFiltStateNum[NP],
         gpswPostFiltStateDenom[NP],
         swPostEmphasisState,
         pswSynthFiltState[NP],
         pswOldFrmKsDec[NP],
         pswOldFrmAsDec[NP],
         pswOldFrmPFNum[NP],
         pswOldFrmPFDenom[NP],
         swOldR0Dec,
         pswLtpStateBaseDec[LTP_LEN + S_LEN],
         pswPPreState[LTP_LEN + S_LEN];

  extern Shortword swMuteFlagOld;      /* error concealment */

  extern Longword plSubfrEnergyMem[4]; /* error concealment */
  extern Shortword swLevelMem[4],
         lastR0,                       /* error concealment */
         pswLastGood[18],              /* error concealment */
         swState,
         swLastFlag;                   /* error concealment */

struct gsmhr {
	int dec_reset_flg;

	struct {
		Shortword swOldR0;
		Shortword swOldR0Index;
		struct NormSw psnsWSfrmEngSpace[2 * N_SUB];
		Shortword pswHPFXState[4];
		Shortword pswHPFYState[4];
		Shortword pswOldFrmKs[NP];
		Shortword pswOldFrmAs[NP];
		Shortword pswOldFrmSNWCoefs[NP];
		Shortword pswWgtSpeechSpace[F_LEN + LMAX + CG_INT_MACS / 2];
		Shortword pswSpeech[INBUFFSZ];        /* input speech */
		Shortword swPtch;
		Shortword pswAnalysisState[NP];
		Shortword pswWStateNum[NP],
			 pswWStateDenom[NP];
		Shortword pswLtpStateBase[LTP_LEN + S_LEN];
		Shortword pswHState[NP];
		Shortword pswHNWState[HNW_BUFF_LEN];
	} encoder;
	struct {
		Shortword gswPostFiltAgcGain,
			 gpswPostFiltStateNum[NP],
			 gpswPostFiltStateDenom[NP],
			 swPostEmphasisState,
			 pswSynthFiltState[NP],
			 pswOldFrmKsDec[NP],
			 pswOldFrmAsDec[NP],
			 pswOldFrmPFNum[NP],
			 pswOldFrmPFDenom[NP],
			 swOldR0Dec,
			 pswLtpStateBaseDec[LTP_LEN + S_LEN],
			 pswPPreState[LTP_LEN + S_LEN];
		Shortword swMuteFlagOld;
		Longword plSubfrEnergyMem[4];
		Shortword swLevelMem[4],
			 lastR0,
			 pswLastGood[18],
			 swState,
			 swLastFlag;
	} decoder;
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

	/* recall state */
	swOldR0 = swOldR0;
	swOldR0Index = swOldR0Index;
	memcpy(psnsWSfrmEngSpace, state->encoder.psnsWSfrmEngSpace, sizeof(struct NormSw) * 2 * N_SUB);
	memcpy(pswHPFXState, state->encoder.pswHPFXState, sizeof(Shortword) * 4);
	memcpy(pswHPFYState, state->encoder.pswHPFYState, sizeof(Shortword) * 4);
	memcpy(pswOldFrmKs, state->encoder.pswOldFrmKs, sizeof(Shortword) * NP);
	memcpy(pswOldFrmAs, state->encoder.pswOldFrmAs, sizeof(Shortword) * NP);
	memcpy(pswOldFrmSNWCoefs, state->encoder.pswOldFrmSNWCoefs, sizeof(Shortword) * NP);
	memcpy(pswWgtSpeechSpace, state->encoder.pswWgtSpeechSpace, sizeof(Shortword) * F_LEN + LMAX + CG_INT_MACS / 2);
	memcpy(pswSpeech, state->encoder.pswSpeech, sizeof(Shortword) * INBUFFSZ);
	swPtch = state->encoder.swPtch;
	memcpy(pswAnalysisState, state->encoder.pswAnalysisState, sizeof(Shortword) * NP);
	memcpy(pswWStateNum, state->encoder.pswWStateNum, sizeof(Shortword) * NP);
	memcpy(pswWStateDenom, state->encoder.pswWStateDenom, sizeof(Shortword) * NP);
	memcpy(pswLtpStateBase, state->encoder.pswLtpStateBase, sizeof(Shortword) * LTP_LEN + S_LEN);
	memcpy(pswHState, state->encoder.pswHState, sizeof(Shortword) * NP);
	memcpy(pswHNWState, state->encoder.pswHNWState, sizeof(Shortword) * HNW_BUFF_LEN);

	memcpy(pcm_b, pcm, F_LEN*sizeof(int16_t));

	enc_reset_flg = encoderHomingFrameTest(pcm_b);

	speechEncoder(pcm_b, hr_params);

	if (enc_reset_flg)
		resetEnc();

	/* store state */
	state->encoder.swOldR0 = swOldR0;
	state->encoder.swOldR0Index = swOldR0Index;
	memcpy(state->encoder.psnsWSfrmEngSpace, psnsWSfrmEngSpace, sizeof(struct NormSw) * 2 * N_SUB);
	memcpy(state->encoder.pswHPFXState, pswHPFXState, sizeof(Shortword) * 4);
	memcpy(state->encoder.pswHPFYState, pswHPFYState, sizeof(Shortword) * 4);
	memcpy(state->encoder.pswOldFrmKs, pswOldFrmKs, sizeof(Shortword) * NP);
	memcpy(state->encoder.pswOldFrmAs, pswOldFrmAs, sizeof(Shortword) * NP);
	memcpy(state->encoder.pswOldFrmSNWCoefs, pswOldFrmSNWCoefs, sizeof(Shortword) * NP);
	memcpy(state->encoder.pswWgtSpeechSpace, pswWgtSpeechSpace, sizeof(Shortword) * F_LEN + LMAX + CG_INT_MACS / 2);
	memcpy(state->encoder.pswSpeech, pswSpeech, sizeof(Shortword) * INBUFFSZ);
	state->encoder.swPtch = swPtch;
	memcpy(state->encoder.pswAnalysisState, pswAnalysisState, sizeof(Shortword) * NP);
	memcpy(state->encoder.pswWStateNum, pswWStateNum, sizeof(Shortword) * NP);
	memcpy(state->encoder.pswWStateDenom, pswWStateDenom, sizeof(Shortword) * NP);
	memcpy(state->encoder.pswLtpStateBase, pswLtpStateBase, sizeof(Shortword) * LTP_LEN + S_LEN);
	memcpy(state->encoder.pswHState, pswHState, sizeof(Shortword) * NP);
	memcpy(state->encoder.pswHNWState, pswHNWState, sizeof(Shortword) * HNW_BUFF_LEN);

	return 0;
}

EXPORT int
gsmhr_decode(struct gsmhr *state, int16_t *pcm, const int16_t *hr_params)
{
#define WHOLE_FRAME		18
#define TO_FIRST_SUBFRAME	 9

	int dec_reset_flg;
	Shortword hr_params_b[22];

	/* recall state */
	gswPostFiltAgcGain = state->decoder.gswPostFiltAgcGain;
	memcpy(gpswPostFiltStateNum, state->decoder.gpswPostFiltStateNum, sizeof(Shortword) * NP);
	memcpy(gpswPostFiltStateDenom, state->decoder.gpswPostFiltStateDenom, sizeof(Shortword) * NP);
	swPostEmphasisState = state->decoder.swPostEmphasisState;
	memcpy(pswSynthFiltState, state->decoder.pswSynthFiltState, sizeof(Shortword) * NP);
	memcpy(pswOldFrmKsDec, state->decoder.pswOldFrmKsDec, sizeof(Shortword) * NP);
	memcpy(pswOldFrmAsDec, state->decoder.pswOldFrmAsDec, sizeof(Shortword) * NP);
	memcpy(pswOldFrmPFNum, state->decoder.pswOldFrmPFNum, sizeof(Shortword) * NP);
	memcpy(pswOldFrmPFDenom, state->decoder.pswOldFrmPFDenom, sizeof(Shortword) * NP);
	swOldR0Dec = state->decoder.swOldR0Dec;
	memcpy(pswLtpStateBaseDec, state->decoder.pswLtpStateBaseDec, sizeof(Shortword) * LTP_LEN + S_LEN);
	memcpy(pswPPreState, state->decoder.pswPPreState, sizeof(Shortword) * LTP_LEN + S_LEN);
	swMuteFlagOld = state->decoder.swMuteFlagOld;
	memcpy(plSubfrEnergyMem, state->decoder.plSubfrEnergyMem, sizeof(Longword) * 4);
	memcpy(swLevelMem, state->decoder.swLevelMem, sizeof(Shortword) * 4);
	lastR0 = state->decoder.lastR0;
	memcpy(pswLastGood, state->decoder.pswLastGood, sizeof(Shortword) * 18);
	swState = state->decoder.swState;
	swLastFlag = state->decoder.swLastFlag;

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

	/* store state */
	state->decoder.gswPostFiltAgcGain = gswPostFiltAgcGain;
	memcpy(state->decoder.gpswPostFiltStateNum, gpswPostFiltStateNum, sizeof(Shortword) * NP);
	memcpy(state->decoder.gpswPostFiltStateDenom, gpswPostFiltStateDenom, sizeof(Shortword) * NP);
	state->decoder.swPostEmphasisState = swPostEmphasisState;
	memcpy(state->decoder.pswSynthFiltState, pswSynthFiltState, sizeof(Shortword) * NP);
	memcpy(state->decoder.pswOldFrmKsDec, pswOldFrmKsDec, sizeof(Shortword) * NP);
	memcpy(state->decoder.pswOldFrmAsDec, pswOldFrmAsDec, sizeof(Shortword) * NP);
	memcpy(state->decoder.pswOldFrmPFNum, pswOldFrmPFNum, sizeof(Shortword) * NP);
	memcpy(state->decoder.pswOldFrmPFDenom, pswOldFrmPFDenom, sizeof(Shortword) * NP);
	state->decoder.swOldR0Dec = swOldR0Dec;
	memcpy(state->decoder.pswLtpStateBaseDec, pswLtpStateBaseDec, sizeof(Shortword) * LTP_LEN + S_LEN);
	memcpy(state->decoder.pswPPreState, pswPPreState, sizeof(Shortword) * LTP_LEN + S_LEN);
	state->decoder.swMuteFlagOld = swMuteFlagOld;
	memcpy(state->decoder.plSubfrEnergyMem, plSubfrEnergyMem, sizeof(Longword) * 4);
	memcpy(state->decoder.swLevelMem, swLevelMem, sizeof(Shortword) * 4);
	state->decoder.lastR0 = lastR0;
	memcpy(state->decoder.pswLastGood, pswLastGood, sizeof(Shortword) * 18);
	state->decoder.swState = swState;
	state->decoder.swLastFlag = swLastFlag;

	return 0;
}
