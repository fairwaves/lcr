
void *gsm_fr_create(void);
void gsm_fr_destroy(void *arg);
int gsm_fr_decode(void *arg, unsigned char *frame, signed short *samples);
void gsm_fr_encode(void *arg, signed short *samples, unsigned char *frame);

