/*
 * SS5 signal coder header file
 */

void ss5_sine_generate(void);
unsigned char *ss5_encode(unsigned char *buffer, int len, char digit, int sample_nr);

