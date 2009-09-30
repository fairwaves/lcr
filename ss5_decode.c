/*
 * SS5 signal decoder.
 *
 * Copyright            by Andreas Eversberg (jolly@eversberg.eu)
 *			based on different decoders such as ISDN4Linux
 *			copyright by Karsten Keil
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include "main.h"
#include "ss5_decode.h"

/* enable level debugging */
//#define DEBUG_LEVELS

#define NCOEFF		8	/* number of frequencies to be analyzed */

#define MIN_DB		0.01995262 /* -17 db */
#define DIFF_DB		0.2 // 0.31622777 /*  -5 db */
#define SNR		1.3	/* noise may not exceed signal by that factor */

/* For DTMF recognition:
 * 2 * cos(2 * PI * k / N) precalculated for all k
 */
static signed long long cos2pik[NCOEFF] =
{
	/* k = 2*cos(2*PI*f/8000), k << 15 
	 * 700, 900, 1100, 1300, 1500, 1700, 2400, 2600 */
	55879, 49834, 42562, 34242, 25080, 15299, -20252, -29753
};

/* detection matrix for two frequencies */
static char decode_two[8][8] =
{
	{' ', '1', '2', '4', '7', '*', ' ', ' '}, /* * = code 11 */
	{'1', ' ', '3', '5', '8', '#', ' ', ' '}, /* # = code 12 */
	{'2', '3', ' ', '6', '9', 'a', ' ', ' '}, /* a = KP1 */
	{'4', '5', '6', ' ', '0', 'b', ' ', ' '}, /* b = KP2 */
	{'7', '8', '9', '0', ' ', 'c', ' ', ' '}, /* c = ST */
	{'*', '#', 'a', 'b', 'c', ' ', ' ', ' '},
	{' ', ' ', ' ', ' ', ' ', ' ', ' ', 'C'}, /* C = 2600+2400 */
	{' ', ' ', ' ', ' ', ' ', ' ', 'C', ' '}
};

static char decode_one[8] =
	{' ', ' ', ' ', ' ', ' ', ' ', 'A', 'B'}; /* A = 2400, B = 2600 */
/*
 * calculate the coefficients of the given sample and decode
 */

char ss5_decode(unsigned char *data, int len)
{
	signed short buf[len];
	signed long sk, sk1, sk2, low, high;
	int k, n, i;
	int f1 = 0, f2 = 0;
	double result[NCOEFF], power, noise, snr;
	signed long long cos2pik_;
	char digit = ' ';

	/* convert samples */
	for (i = 0; i < len; i++)
		buf[i] = audio_law_to_s32[*data++];

	/* now we have a full buffer of signed long samples - we do goertzel */
	for (k = 0; k < NCOEFF; k++) {
		sk = 0;
		sk1 = 0;
		sk2 = 0;
		cos2pik_ = cos2pik[k];
		for (n = 0; n < len; n++) {
			sk = ((cos2pik_*sk1)>>15) - sk2 + buf[n];
			sk2 = sk1;
			sk1 = sk;
		}
		sk >>= 8;
		sk2 >>= 8;
		if (sk > 32767 || sk < -32767 || sk2 > 32767 || sk2 < -32767)
			PERROR("Tone-Detection overflow\n");
		/* compute |X(k)|**2 */
		result[k] = sqrt (
				(sk * sk) -
				(((cos2pik[k] * sk) >> 15) * sk2) +
				(sk2 * sk2)
			) / len / 62; /* level of 1 is 0 db*/
	}

	/* now we do noise level calculation */
	low = 32767;
	high = -32768;
	for (n = 0; n < len; n++) {
		sk = buf[n];
		if (sk < low)
			low = sk;
		if (sk > high)
			high = sk;
	}
	noise = ((double)(high-low) / 65536.0);

	/* find the two loudest frequencies + one less lower frequency to detect noise */
	power = 0.0;
	for (i = 0; i < NCOEFF; i++) {
		if (result[i] > power) {
			power = result[i];
			f1 = i;
		}
	}
	power = 0.0;
	for (i = 0; i < NCOEFF; i++) {
		if (i != f1 && result[i] > power) {
			power = result[i];
			f2 = i;
		}
	}

	snr = 0;
	/* check one frequency */
	if (result[f1] > MIN_DB /* must be at least -17 db */
	 && result[f1]*SNR > noise) { /*  */
		digit = decode_one[f1];
		if (digit != ' ')
			snr = result[f1] / noise;
	}
	/* check two frequencies */
	if (result[f1] > MIN_DB && result[f2] > MIN_DB /* must be at lease -17 db */
	 && result[f1]*DIFF_DB <= result[f2] /* f2 must be not less than 5 db below f1 */
	 && (result[f1]+result[f2])*SNR > noise) { /* */
		digit = decode_two[f1][f2];
		if (digit != ' ')
			snr = (result[f1]+result[f2]) / noise;
	}

	/* debug powers */
#ifdef DEBUG_LEVELS
	if (noise > 0.2) {
		for (i = 0; i < NCOEFF; i++)
			printf("%d:%3d %c ", i, (int)(result[i]*100), (f1==i || f2==i)?'*':' ');
		printf("N:%3d digit:%c snr=%3d\n", (int)(noise*100), digit, (int)(snr*100));
	 	if (result[f1]*DIFF_DB <= result[f2]) /* f2 must be not less than 5 db below f1 */
			printf("jo!");
	}
#endif

	return digit;
}

void ss5_test_decode(void)
{
#ifdef DEBUG_LEVELS
	double phase;
	int i, j;
	signed short sample;

	unsigned char buffer[SS5_DECODER_NPOINTS];
	for (i = 0; i < 4000; i += 10) {
		phase = 2.0 * 3.14159265 * i / 8000.0;
		for (j = 0; j < SS5_DECODER_NPOINTS; j++) {
			sample = sin(phase * j) * 1000;
			buffer[j] = audio_s16_to_law[sample & 0xffff];
		}
		printf("FRQ:%04d:", i);
		ss5_decode(buffer, SS5_DECODER_NPOINTS);
	}
#endif
}

