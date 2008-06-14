/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** tones header file                                                         **
**                                                                           **
\*****************************************************************************/ 

int open_tone(char *file, int *codec, signed int *length, signed int *left);
int read_tone(int fh, unsigned char *buffer, int codec, int len, signed int size, signed int *left, int speed);
int fetch_tones(void);
void free_tones(void);
void *open_tone_fetched(char *dir, char *file, int *codec, signed int *length, signed int *left);
int read_tone_fetched(void **fetched, void *buffer, int len, signed int size, signed int *left, int speed);

/* tone sets */
struct tonesettone {
	struct tonesettone *next;
	char name[128];
	int codec;
	int size;
	unsigned char data[0];
	};

struct toneset {
	struct toneset *next;
	char directory[128];
	struct tonesettone *first;
	};

extern struct toneset *toneset_first;


