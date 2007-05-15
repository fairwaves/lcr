/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** opening and reading tone                                                  **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include "main.h"

/* 
notes about codecs:

CODEC_OFF is a none accepted value
CODEC_LAW is 8 bit (1 byte) data 8khz
other codecs are 16 bit (2 bytes) data 8khz
the read_tone() will return law or 16bit mono. the read_tone will convert all other formats to 16bit mono.

*/ 


/*
 * open the tone (don't increase fhuse, since it is done after calling this function)
 * NOTE: length and left will be set to the number of samples, NOT bytes
 */
struct fmt {
	unsigned short  stereo; /* 1 = pcm, 2 = adpcm */
	unsigned short  channels; /* number of channels */
	unsigned long   sample_rate; /* sample rate */
	unsigned long   data_rate; /* data rate */
	unsigned short  bytes_sample; /* bytes per sample (all channels) */
	unsigned short  bits_sample; /* bits per sample (one channel) */
};
int open_tone(char *file, int *codec, signed long *length, signed long *left)
{
	int fh;
	char filename[256];        
	char linkname[256];        
	unsigned char buffer[256];
	struct fmt *fmt;
	int channels, bytes;
	unsigned long size, chunk;
	int gotfmt = 0;
	struct stat _stat;
	int linksize;
	int l;
	char *p;


	/* try to open the law file */
	SPRINT(filename, "%s.isdn", file);
	if ((fh = open(filename, O_RDONLY)) >= 0)
	{
		/* stat tone */
		l = 0;
		while(42)
		{
			if (l >= 10)
			{
				close(fh);
				PERROR("Link chain too deep: '%s'\n", filename);
				return(-1);
			}
			if (lstat(filename, &_stat) == -1)
			{
				close(fh);
				PERROR("Cannot stat file: '%s'\n", filename);
				return(-1);
			}
			if (!S_ISLNK(_stat.st_mode))
			{
				break;
			}
			if ((linksize=readlink(filename, linkname, sizeof(linkname))) > 0)
			{
				linkname[linksize] = '\0';
			} else
			{
				close(fh);
				PERROR("Cannot read link information: '%s'\n", filename);
				return(-1);
			}
			if (linkname[0] == '/') /* absolute link */
			{
				SCPY(filename, linkname);
			} else /* relative link */
			{
				/* remove filename */
				p = filename;
				while(strchr(p, '/'))
				{
					p = strchr(p, '/')+1;
				}
				*p = 0;
				/* concat the link */
				SCAT(filename, linkname);
			}
//printf("follow link: %s\n", filename);
			l++;
		}
		if (length)
			*length = _stat.st_size;
		if (left)
			*left = _stat.st_size;
		if (codec)
			*codec = CODEC_LAW;
		return(fh);
	}

	/* try to open the wave file */
	SPRINT(filename, "%s.wav", file);
	if ((fh = open(filename, O_RDONLY)) >= 0)
	{
		/* get wave header */
		read(fh, buffer, 8);
		size=(buffer[4]) + (buffer[5]<<8) + (buffer[6]<<16) + (buffer[7]<<24);
		if (!!strncmp((char *)buffer, "RIFF", 4))
		{
			close(fh);
			errno = 0;
			PERROR("%s is no riff file!\n", filename);
			return(-1);
		}
//		printf("%c%c%c%c size=%ld\n",buffer[0],buffer[1],buffer[2],buffer[3],size);
		read(fh, buffer, 4);
		size -= 4;
		if (!!strncmp((char *)buffer, "WAVE", 4))
		{
			close(fh);
			errno = 0;
			PERROR("%s is no wave file!\n", filename);
			return(-1);
		}
		while(size)
		{
			if (size>0 && size<8)
			{
				close(fh);
				errno = 0;
				PERROR("Remaining file size %ld not large enough for next chunk.\n",size);
				return(-1);
			}
			read(fh, buffer, 8);
			chunk=(buffer[4]) + (buffer[5]<<8) + (buffer[6]<<16) + (buffer[7]<<24);
			size -= (8+chunk);
//			printf("%c%c%c%c lenght=%d\n",buffer[0],buffer[1],buffer[2],buffer[3],chunk);
			if (size < 0)
			{
				close(fh);
				errno = 0;
				PERROR("Chunk '%c%c%c%c' is larger than remainig file size (length=%ld)\n",buffer[0],buffer[1],buffer[2],buffer[3], chunk);
				return(-1);
			}
			if (!strncmp((char *)buffer, "fmt ", 4))
			{
				if (chunk != 16)
				{
					close(fh);
					errno = 0;
					PERROR("File %s Fmt chunk illegal size.\n", filename);
					return(-1);
				}
				read(fh, buffer, chunk);
				fmt = (struct fmt *)buffer;
				if (fmt->channels<1 || fmt->channels>2)
				{
					close(fh);
					errno = 0;
					PERROR("File %s Only support one or two channels file.\n", filename);
					return(-1);
				}
				channels = fmt->channels;
//				printf("Channels: %d\n", channels);
				if (fmt->sample_rate != 8000)
				{
					PERROR("Warning: File %s has sample rate of %ld.\n", filename, fmt->sample_rate);
				}
//				printf("Sample Rate: %ld\n", fmt->sample_rate);
				if (fmt->bits_sample!=8 && fmt->bits_sample!=16)
				{
					close(fh);
					errno = 0;
					PERROR("File %s has neigher 8 nor 16 bit samples.\n", filename);
					return(-1);
				}
				bytes = (fmt->bits_sample==16)?2:1;
//				printf("Bit-Resolution: %d\n", bytes*16-16);
				gotfmt = 1;
			} else
			if (!strncmp((char *)buffer, "data", 4))
			{
				if (!gotfmt)
				{
					close(fh);
					errno = 0;
					PERROR("File %s No fmt chunk found before data chunk.\n", filename);
					return(-1);
				}
//				printf("Length: %ld samples (%ld.%03ld seconds)\n", chunk/bytes/channels, chunk/bytes/channels/8000, ((chunk/bytes/channels)%8000)*1000/8000);
				if (bytes==2 && channels==1)
				{
					if (codec)
						*codec = CODEC_MONO;
					if (length)
						*length = ((signed long)chunk)>>1;
					if (left)
						*left = ((signed long)chunk)>>1;
				} else
				if (bytes==2 && channels==2)
				{
					if (codec)
						*codec = CODEC_STEREO;
					if (length)
						*length = ((signed long)chunk)>>2;
					if (left)
						*left = ((signed long)chunk)>>2;
				} else
				if (bytes==1 && channels==1)
				{
					if (codec)
						*codec = CODEC_8BIT;
					if (length)
						*length = (signed long)chunk;
					if (left)
						*left = (signed long)chunk;
				} else
				{
					close(fh);
					errno = 0;
					PERROR("File %s Is not MONO8, MONO16 nor STEREO16.\n", filename);
					return(-1);
				}
				return(fh);
			} else
			{
//				PDEBUG(DEBUG_PORT, "Unknown chunk '%c%c%c%c'\n",buffer[0],buffer[1],buffer[2],buffer[3]);
				while(chunk > sizeof(buffer))
				{
					read(fh, buffer, sizeof(buffer));
					chunk -=  sizeof(buffer);
				}
				if (chunk)
					read(fh, buffer, chunk);
			}
			
		}
		if (!gotfmt)
		{
			close(fh);
			errno = 0;
			PERROR("File %s No fmt chunk found in file.\n", filename);
			return(-1);
		}
		close(fh);
		errno = 0;
		PERROR("File %s No data chunk found in file.\n", filename);
		return(-1);
	}

	return(-1);
}


/*
 * read from tone, check size
 * the len must be the number of samples, NOT for the bytes to read!!
 * the data returned is law-code
 */
int read_tone(int fh, void *buffer, int codec, int len, signed long size, signed long *left, int speed)
{
	int l;
	int offset;
//printf("left=%ld\n",*left);

	/* if no *left is given (law has unknown length) */
	if (!left)
		goto unknown_length;

	if (speed!=1)
	{
		offset = ((len&(~4)) * (speed-1));
		lseek(fh, offset, SEEK_CUR); /* step fowards, backwards (len must be round to 4 bytes, to be sure, that 16bit stereo will not drift out of sync)*/
		*left -= offset; /* correct the current bytes left */
		if (*left < 0)
		{
			/* eof */
			*left = 0;
			return(0);
		}
		if (*left >= size)
		{
			/* eof */
			*left = size;
			return(0);
		}
	}

	if (*left == 0)
		return(0);

	if (*left < len)
		len = *left;
	unknown_length:
	switch(codec)
	{
		case CODEC_LAW:
		l = read(fh, buffer, len); /* as is */
		break;

		case CODEC_MONO:
			signed short buffer16[len], *buf16 = buffer16;
			signed long sample;
			int i = 0;
			l = read(fh, buf16, len<<1);
			if (l>0)
			{
				l = l>>1;
				while(i < l)
				{
					sample = *buf16++;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*buffer++ = audio_s16_to_law[sample & 0xffff];
					i++;
				}
			}
		break;

		case CODEC_STEREO:
		{
			signed short buffer32[len<<1], *buf32 = buffer32;
			signed long sample;
			int i = 0;
			l = read(fh, buf32, len<<2);
			if (l>0)
			{
				l = l>>2;
				while(i < l)
				{
					sample = (*buf32++) + (*buf32++);
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*buffer++ = audio_s16_to_law[sample & 0xffff];
					i++;
				}
			}
		}
		break;

		case CODEC_8BIT:
		{
			unsigned char buffer8[len], *buf8 = buffer8;
			int i = 0;
			l = read(fh, buf8, len);
			if (l>0)
			{
				while(i < l)
				{
					*buffer++ = audio_s16_to_law[(((*buf8++)<<8)-0x8000) & 0xffff];
					i++;
				}
			}
		}
		break;

		default:
		PERROR("codec %d is not supported, exitting...\n", codec);
		exit(-1);
	}

	if (l>0 && left)
		*left -= l;
	return(l);
}


struct toneset *toneset_first = NULL;

/*
 * free fetched tones
 */
void free_tones(void)
{
	struct toneset *toneset_temp;
	struct tonesettone *tonesettone_temp;
	void *temp;

	toneset_temp = toneset_first;
	while(toneset_temp)
	{
		tonesettone_temp = toneset_temp->first;
		while(tonesettone_temp)
		{
			temp = tonesettone_temp;
			tonesettone_temp = tonesettone_temp->next;
			free(temp);
			memuse--;
		}
		temp = toneset_temp;
		toneset_temp = toneset_temp->next;
		free(temp);
		memuse--;
	}
	toneset_first = NULL;
}

/*
 * fetch tones as specified in options.conf
 */
int fetch_tones(void)
{
	DIR *dir;
	struct dirent *dirent;
	struct toneset **toneset_nextpointer;
	struct tonesettone **tonesettone_nextpointer;
	char *p, *p_next;
	char path[256];
	char filename[256], name[256];
	int fh;
	int tone_codec;
	signed long tone_size, tone_left;
	unsigned long memory = 0;
	int samples = 0;

	/* if disabled */
	if (!options.fetch_tones)
		return(1);

	toneset_nextpointer = &toneset_first;
	p = options.fetch_tones;
	if (*p == '\0')
		return(1);

	while (*p)
	{
		p_next = p;
		while(*p_next)
		{
			if (*p_next == ',')
			{
				*p_next = '\0';
				p_next++;
				break;
			}
			p_next++;
		}

		/* remove trailing / */
		if (*p) if (p[strlen(p)-1] == '/')
			p[strlen(p)-1] = '\0';

		printf("PBX: Fetching tones '%s'\n", p);
		PDEBUG(DEBUG_PORT, "fetching tones directory '%s'\n", p);

		*toneset_nextpointer = (struct toneset *)calloc(1, sizeof(struct toneset));
		if (*toneset_nextpointer == NULL)
		{
			PERROR("No memory for tone set: '%s'\n",p);
			return(0);
		}
		memuse++;
		memory += sizeof(struct toneset);
		memset(*toneset_nextpointer, 0 , sizeof(struct toneset));
		SCPY((*toneset_nextpointer)->directory, p);
		tonesettone_nextpointer = &(*toneset_nextpointer)->first;

		SPRINT(path, "%s/%s", INSTALL_DATA, p);
		dir = opendir(path);
		if (dir == NULL)
		{
			PERROR("Tone set not found: '%s'\n", path);
			return(0);
		}

		while((dirent=readdir(dir)))
		{
			SPRINT(name, "%s", dirent->d_name);

			/* remove .isdn and .wave */
			if (strlen(name) >= 4)
			{
				if (!strcmp(name+strlen(name)-4, ".wav"))
					name[strlen(name)-4] = '\0';
			}
			if (strlen(name) >= 5)
			{
				if (!strcmp(name+strlen(name)-5, ".isdn"))
					name[strlen(name)-5] = '\0';
			}

			SPRINT(filename, "%s/%s", path, name);

			/* skip . / .. */
			if (!strcmp(dirent->d_name, "."))
				continue;
			if (!strcmp(dirent->d_name, ".."))
				continue;

			/* open file */
			fh = open_tone(filename, &tone_codec, &tone_size, &tone_left);
			if (fh < 0)
			{
				PERROR("Cannot open file: '%s'\n", filename);
				continue;
			}
			fduse++;

			if (tone_size < 0)
			{
				PERROR("File has 0-length: '%s'\n", filename);
				close(fh);
				fduse--;
				continue;
			}

			/* allocate tone */
			*tonesettone_nextpointer = (struct tonesettone *)calloc(1, sizeof(struct tonesettone)+tone_size);
			if (*toneset_nextpointer == NULL)
			{
				PERROR("No memory for tone set: '%s'\n",p);
				close(fh);
				fduse--;
				return(0);
			}
			memuse++;
//printf("tone:%s, %ld bytes\n", name, tone_size);
			memset(*tonesettone_nextpointer, 0 , sizeof(struct tonesettone)+tone_size);
			memory += sizeof(struct tonesettone)+tone_size;
			samples ++;

			/* load tone */
			read_tone(fh, (*tonesettone_nextpointer)->data, tone_codec, tone_size, tone_size, &tone_left, 1);
			(*tonesettone_nextpointer)->size = tone_size;
			(*tonesettone_nextpointer)->codec = (tone_codec==CODEC_LAW)?CODEC_LAW:CODEC_MONO;
			SCPY((*tonesettone_nextpointer)->name, name);

			close(fh);
			fduse--;
				 
			tonesettone_nextpointer = &((*tonesettone_nextpointer)->next);
		}

		toneset_nextpointer = &((*toneset_nextpointer)->next);
		p = p_next;
	}

	printf("PBX: Memory used for tones: %ld bytes (%d samples)\n", memory, samples);
	PDEBUG(DEBUG_PORT, "Memory used for tones: %ld bytes (%d samples)\n", memory, samples);

	return(1);
} 


/*
 * opens the fetched tone (if available)
 */
void *open_tone_fetched(char *dir, char *file, int *codec, signed long *length, signed long *left)
{
	struct toneset *toneset;
	struct tonesettone *tonesettone;

	/* if anything fetched */
	if (!toneset_first)
		return(NULL);

	/* find set */
	toneset = toneset_first;
	while(toneset)
	{
//printf("1. comparing '%s' with '%s'\n", toneset->directory, dir);
		if (!strcmp(toneset->directory, dir))
			break;
		toneset = toneset->next;
	}
	if (!toneset)
		return(NULL);

	/* find tone */
	tonesettone = toneset->first;
	while(tonesettone)
	{
//printf("2. comparing '%s' with '%s'\n", tonesettone->name, file);
		if (!strcmp(tonesettone->name, file))
			break;
		tonesettone = tonesettone->next;
	}
	if (!tonesettone)
		return(NULL);

	/* return information */
	if (length)
		*length = tonesettone->size;
	if (left)
		*left = tonesettone->size;
	if (codec)
		*codec = tonesettone->codec;
//printf("size=%ld, data=%08x\n", tonesettone->size, tonesettone->data);
	return(tonesettone->data);
}


/*
 * read from fetched tone, check size
 * the len must be the number of samples, NOT for the bytes to read!!
 */
int read_tone_fetched(void **fetched, void *buffer, int len, signed long size, signed long *left, int speed)
{
	int l;
//printf("left=%ld\n",*left);

	/* if no *left is given (law has unknown length) */
	if (!left)
		return(0);

	if (*left == 0)
		return(0);

	if (*left < len)
		len = *left;

	memcpy(buffer, *fetched, len);
	*((char **)fetched) += len;
	l = len;

	if (l>0 && left)
		*left -= l;
	return(l);
}

