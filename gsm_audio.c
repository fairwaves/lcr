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

} /* extern "C" */

