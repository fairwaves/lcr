/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** gsm audio                                                                 **
**                                                                           **
\*****************************************************************************/ 

extern "C" {
#include "libgsmfr/inc/gsm.h"


/* create gsm instance */
void *gsm_fr_create(void)
{
	int value = 1;
	gsm handle;

	handle = gsm_create();
	if (handle)
		gsm_option(handle, 0/*GSM_OPT_WAV49*/, &value);

	return handle;
}

/* free gsm instance */
void gsm_fr_destroy(void *arg)
{
	gsm_destroy((gsm)arg);
}

/* decode frame into samples, return error */
int gsm_fr_decode(void *arg, unsigned char *frame, signed short *samples)
{
//	int value = 0;

//	gsm_option((gsm)arg, GSM_OPT_FRAME_INDEX, &value);
	return gsm_decode((gsm)arg, (gsm_byte *)frame, (gsm_signal *)samples);
}

/* encode samples into frame */
void gsm_fr_encode(void *arg, signed short *samples, unsigned char *frame)
{
//	int value = 0;
	
//	gsm_option((gsm)arg, GSM_OPT_FRAME_INDEX, &value);
	gsm_encode((gsm)arg, (gsm_signal *)samples, (gsm_byte *)frame);
}

#ifdef WITH_GSMAMR

#include <stdlib.h>
#include <string.h>

#include <opencore-amrnb/interf_dec.h>
#include <opencore-amrnb/interf_enc.h>


struct codec_efr_state {
	void *encoder;
	void *decoder;
};

/* create gsm instance */
void *gsm_amr_create(void)
{
	struct codec_efr_state *st;

	st = (struct codec_efr_state *)calloc(1, sizeof(*st));
	if (!st)
		return NULL;

	st->encoder = Encoder_Interface_init(0);
	st->decoder = Decoder_Interface_init();

	return (void *)st;
}

/* free gsm instance */
void gsm_amr_destroy(void *arg)
{
	struct codec_efr_state *st = (struct codec_efr_state *)arg;

	Decoder_Interface_exit(st->decoder);
	Encoder_Interface_exit(st->encoder);

	return;
}

enum Mode amr_mode[8] = {
        MR475,    /* 4.75 kbps */
        MR515,    /* 5.15 kbps */
	MR59,     /* 5.90 kbps */
	MR67,     /* 6.70 kbps */
	MR74,     /* 7.40 kbps */
	MR795,    /* 7.95 kbps */
	MR102,    /* 10.2 kbps */
	MR122,    /* 12.2 kbps */
};

/* decode frame into samples, return error */
int gsm_amr_decode(void *arg, unsigned char *frame, signed short *samples)
{
	struct codec_efr_state *st = (struct codec_efr_state *)arg;

	Decoder_Interface_Decode(
		st->decoder,
		(const unsigned char*) frame + 1,
		(short *) samples,
		0
	);

	return 0;
}

/* encode samples into frame */
int gsm_amr_encode(void *arg, signed short *samples, unsigned char *frame, int mode)
{
	struct codec_efr_state *st = (struct codec_efr_state *)arg;
	int rv;

	rv = Encoder_Interface_Encode(
		st->encoder,
		amr_mode[mode],
		(const short*) samples,
		(unsigned char*) frame + 1,
		1
	);

	frame[0] = 0xf0; /* no request */

	return rv + 1;
}

const unsigned short gsm690_12_2_bitorder[244] = {
	  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
	 10,  11,  12,  13,  14,  23,  15,  16,  17,  18,
	 19,  20,  21,  22,  24,  25,  26,  27,  28,  38,
	141,  39, 142,  40, 143,  41, 144,  42, 145,  43,
	146,  44, 147,  45, 148,  46, 149,  47,  97, 150,
	200,  48,  98, 151, 201,  49,  99, 152, 202,  86,
	136, 189, 239,  87, 137, 190, 240,  88, 138, 191,
	241,  91, 194,  92, 195,  93, 196,  94, 197,  95,
	198,  29,  30,  31,  32,  33,  34,  35,  50, 100,
	153, 203,  89, 139, 192, 242,  51, 101, 154, 204,
	 55, 105, 158, 208,  90, 140, 193, 243,  59, 109,
	162, 212,  63, 113, 166, 216,  67, 117, 170, 220,
	 36,  37,  54,  53,  52,  58,  57,  56,  62,  61,
	 60,  66,  65,  64,  70,  69,  68, 104, 103, 102,
	108, 107, 106, 112, 111, 110, 116, 115, 114, 120,
	119, 118, 157, 156, 155, 161, 160, 159, 165, 164,
	163, 169, 168, 167, 173, 172, 171, 207, 206, 205,
	211, 210, 209, 215, 214, 213, 219, 218, 217, 223,
	222, 221,  73,  72,  71,  76,  75,  74,  79,  78,
	 77,  82,  81,  80,  85,  84,  83, 123, 122, 121,
	126, 125, 124, 129, 128, 127, 132, 131, 130, 135,
	134, 133, 176, 175, 174, 179, 178, 177, 182, 181,
	180, 185, 184, 183, 188, 187, 186, 226, 225, 224,
	229, 228, 227, 232, 231, 230, 235, 234, 233, 238,
	237, 236,  96, 199,
};

/* decode frame into samples, return error */
int gsm_efr_decode(void *arg, unsigned char *frame, signed short *samples)
{
	struct codec_efr_state *st = (struct codec_efr_state *)arg;
	unsigned char cod[32], bit;
	int i, si;

	cod[0] = 0x3c; /* good AMR 12,2 frame */
	memset(cod + 1, 0, 31);

	for (i = 0; i < 244; i++) {
		si = gsm690_12_2_bitorder[i] + 4;
		bit = (frame[si >> 3] >> (7 - (si & 7))) & 1;
		cod[(i >> 3) + 1] |= (bit << (7 - (i & 7)));
	}

	Decoder_Interface_Decode(
		st->decoder,
		(const unsigned char*) cod,
		(short *) samples,
		0
	);

	return 0;
}

/* encode samples into frame */
int gsm_efr_encode(void *arg, signed short *samples, unsigned char *frame)
{
	struct codec_efr_state *st = (struct codec_efr_state *)arg;
	int rv;
	unsigned char cod[32], bit;
	int i, di;

	rv = Encoder_Interface_Encode(
		st->encoder,
		MR122,
		(const short*) samples,
		(unsigned char*) cod,
		1
	);

	if (cod[0] != 0x3c)
		return -1;

	frame[0] = 0xc0;
	memset(frame + 1, 0, 30);

	for (i = 0; i < 244; i++) {
		di = gsm690_12_2_bitorder[i] + 4;
		bit = (cod[(i >> 3) + 1] >> (7 - (i & 7))) & 1;
		frame[di >> 3] |= (bit << (7 - (di & 7)));
	}
	return rv;
}

#endif

#ifdef WITH_GSMHR

#include <gsmhr/gsmhr.h>

/* create gsm instance */
void *gsm_hr_create(void)
{
	struct gsmhr *state;
	
	state = gsmhr_init();
	
	return state;
}

/* free gsm instance */
void gsm_hr_destroy(void *arg)
{
	gsmhr_exit((struct gsmhr *)arg);
}

#include <string.h>
/* decode frame into samples, return error */
int gsm_hr_decode(void *arg, unsigned char *frame, signed short *samples)
{
	int16_t w[22];
	int rc;

	w[0] = frame[1] >> 3;
	w[1] = ((frame[1] & 0x07) << 8) | frame[2];
	w[2] = (frame[3] << 1) | (frame[4] >> 7);
	w[3] = ((frame[4] & 0x7f) << 1) | (frame[5] >> 7);
	w[4] = (frame[5] & 0x7f) >> 6;
	w[5] = (frame[5] & 0x3f) >> 4;
	if (w[5]) {
		/* voiced */
		w[6] = ((frame[5] & 0x0f) << 4) | (frame[6] >> 4);
		w[7] = ((frame[6] & 0x0f) << 5) | (frame[7] >> 3);
		w[8] = ((frame[7] & 0x07) << 2) | (frame[8] >> 6);
		w[9] = (frame[8] & 0x3f) >> 2;
		w[10] = ((frame[8] & 0x03) << 7) | (frame[9] >> 1);
		w[11] = ((frame[9] & 0x01) << 4) | (frame[10] >> 4);
		w[12] = frame[10] & 0x0f;
		w[13] = (frame[11] << 1) | (frame[12] >> 7);
		w[14] = (frame[12] & 0x7f) >> 2;
		w[15] = ((frame[12] & 0x03) << 2) | (frame[13] >> 6);
		w[16] = ((frame[13] & 0x3f) << 3) | (frame[14] >> 5);
		w[17] = frame[14] & 0x1f;
	} else {
		/* unvoiced */
		w[6] = ((frame[5] & 0x0f) << 3) | (frame[6] >> 5);
		w[7] = ((frame[6] & 0x1f) << 2) | (frame[7] >> 6);
		w[8] = (frame[7] & 0x3f) >> 1;
		w[9] = ((frame[7] & 0x01) << 6) | (frame[8] >> 2);
		w[10] = ((frame[8] & 0x03) << 5) | (frame[9] >> 3);
		w[11] = ((frame[9] & 0x07) << 2) | (frame[10] >> 6);
		w[12] = ((frame[10] & 0x3f) << 1) | (frame[11] >> 7);
		w[13] = frame[11] & 0x7f;
		w[14] = frame[12] >> 3;
		w[15] = ((frame[12] & 0x07) << 4) | (frame[13] >> 4);
		w[16] = ((frame[13] & 0x1f) << 3) | (frame[14] >> 5);
		w[17] = frame[14] & 0x1f;
	}
	w[18] = 0;		/* BFI : 1 bit */
	w[19] = 0;		/* UFI : 1 bit */
	w[20] = 0;		/* SID : 2 bit */
	w[21] = 0;		/* TAF : 1 bit */

	rc = gsmhr_decode((struct gsmhr *)arg, samples, w);

	if (rc < 0)
		return rc;

	return 0;
}

/* encode samples into frame */
void gsm_hr_encode(void *arg, signed short *samples, unsigned char *frame)
{
	int16_t w[22];

	gsmhr_encode((struct gsmhr *)arg, w, samples);

	if (!frame)
		return;

	frame[0] = 0x00;
	frame[1] = (w[0] << 3) | (w[1] >> 8);
	frame[2] = w[1];
	frame[3] = (w[2] >> 1);
	frame[4] = (w[2] << 7) | (w[3] >> 1);
	frame[5] = (w[3] << 7) | (w[4] << 6) | (w[5] << 4);
	if (w[5]) {
		/* voiced */
		frame[5] |= (w[6] >> 4);
		frame[6] = (w[6] << 4) | (w[7] >> 5);
		frame[7] = (w[7] << 3) | (w[8] >> 2);
		frame[8] = (w[8] << 6) | (w[9] << 2) | (w[10] >> 7);
		frame[9] = (w[10] << 1) | (w[11] >> 4);
		frame[10] = (w[11] << 4) | w[12];
		frame[11] = (w[13] >> 1);
		frame[12] = (w[13] << 7) | (w[14] << 2) | (w[15] >> 2);
		frame[13] = (w[15] << 6) | (w[16] >> 3);
		frame[14] = (w[16] << 5) | w[17];
	} else {
		/* unvoiced */
		frame[5] |= (w[6] >> 3);
		frame[6] = (w[6] << 5) | (w[7] >> 2);
		frame[7] = (w[7] << 6) | (w[8] << 1) | (w[9] >> 6);
		frame[8] = (w[9] << 2) | (w[10] >> 5);
		frame[9] = (w[10] << 3) | (w[11] >> 2);
		frame[10] = (w[11] << 6) | (w[12] >> 1);
		frame[11] = (w[12] << 7) | w[13];
		frame[12] = (w[14] << 3) | (w[15] >> 4);
		frame[13] = (w[15] << 4) | (w[16] >> 3);
		frame[14] = (w[16] << 5) | w[17];
	}
}

#endif

} /* extern "C" */

