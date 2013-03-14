
#ifdef WITH_GSMFR
void *gsm_fr_create(void);
void gsm_fr_destroy(void *arg);
int gsm_fr_decode(void *arg, unsigned char *frame, signed short *samples);
void gsm_fr_encode(void *arg, signed short *samples, unsigned char *frame);
#endif

#ifdef WITH_GSMAMR
void *gsm_amr_create(void);
void gsm_amr_destroy(void *arg);
int gsm_amr_decode(void *arg, unsigned char *frame, signed short *samples);
int gsm_amr_encode(void *arg, signed short *samples, unsigned char *frame, int mode);
int gsm_efr_decode(void *arg, unsigned char *frame, signed short *samples);
int gsm_efr_encode(void *arg, signed short *samples, unsigned char *frame);
#endif

