
void *gsm_audio_create(void);
void gsm_audio_destroy(void *arg);
int gsm_audio_decode(void *arg, unsigned char *frame, signed short *samples);
void gsm_audio_encode(void *arg, signed short *samples, unsigned char *frame);

