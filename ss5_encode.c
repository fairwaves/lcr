/*
 * SS5 signal coder.
 *
 * Copyright            by Andreas Eversberg (jolly@eversberg.eu)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include "main.h"
#include "ss5_encode.h"

	/* 2*PI*f/8000 */
static double ss5_freq[9][2] = {
	{0.0, 0},		/* 0: 0 */
	{700,	0.19952623},	/* 1: 700, -7db */
	{900,	0.19952623},	/* 2: 900, -7db */
	{1100,	0.19952623},	/* 3: 1100, -7db */
	{1300,	0.19952623},	/* 4: 1300, -7db */
	{1500,	0.19952623},	/* 5: 1500, -7db */
	{1700,	0.19952623},	/* 6: 1700, -7db */
	{2400,	0.12589254},	/* 7: 2400, -9db */
	{2600,	0.12589254},	/* 8: 2600, -9db */
};

static char ss5_digits[][3] = {
	{'1', 1, 2},
	{'2', 1, 3},
	{'3', 2, 3},
	{'4', 1, 4},
	{'5', 2, 4},
	{'6', 3, 4},
	{'7', 1, 5},
	{'8', 2, 5},
	{'9', 3, 5},
	{'0', 4, 5},
	{'*', 1, 6}, /* code 11 */
	{'#', 2, 6}, /* code 12 */
	{'a', 3, 6}, /* KP1 */
	{'b', 4, 6}, /* KP2 */
	{'c', 5, 6}, /* ST */
	{'A', 7, 0}, /* 2400 answer, acknowledge */
	{'B', 8, 0}, /* 2600 busy */
	{'C', 7, 8}, /* 2600+2400 clear forward */
	{0 , 0, 0},
};

static unsigned char sintab[15+3][8192]; /* sine tables of about one second sound (error <1Hz) */

/* generate sine tables */
void ss5_sine_generate(void)
{
	int i, j;
	int cycles1, cycles2;
	double vol1, vol2, phase1, phase2;
	signed short sample;

	for (i = 0; i < 15+3; i++) {
		/* how many cycles are within 8192 samples (rounded!) */
		cycles1 = (int)(ss5_freq[(int)ss5_digits[i][1]][0] / 8000.0 * 8192.0 + 0.5);
		cycles2 = (int)(ss5_freq[(int)ss5_digits[i][2]][0] / 8000.0 * 8192.0 + 0.5);
		/* how much phase shift within one cycle */
		phase1 = 2.0 * 3.14159265 * cycles1 / 8192.0;
		phase2 = 2.0 * 3.14159265 * cycles2 / 8192.0;
		/* volume */
		vol1 = ss5_freq[(int)ss5_digits[i][1]][1] * 32768.0;
		vol2 = ss5_freq[(int)ss5_digits[i][2]][1] * 32768.0;
		for (j = 0; j < 8192; j++) {
			sample = (int)(sin(phase1 * j) * vol1);
			sample += (int)(sin(phase2 * j) * vol2);
			sintab[i][j] = audio_s16_to_law[sample & 0xffff];
		}
	}
}

/* encode digit at given sample_nr with given lengt and return law-encoded audio */
unsigned char *ss5_encode(unsigned char *buffer, int len, char digit, int sample_nr)
{
	int i, j;

	/* get frequency from digit */
	i = 0;
	while(ss5_digits[i][0]) {
		if (digit == ss5_digits[i][0])
			break;
		i++;
	}
	if (!ss5_digits[i][0]) {
		PERROR("Digit '%c' does not exist.\n", digit);
		memset(buffer, audio_s16_to_law[0], sizeof(buffer));
		return buffer;
	}

	/* copy tones */
	for (j = 0; j < len; j++)
		*buffer++ = sintab[i][sample_nr++ & 8191];

	return buffer;
}


