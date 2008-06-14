
/* isdn tone generation
  by jolly
*/

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


/* ulaw -> signed 16-bit */
static short isdn_audio_ulaw_to_s16[] =
{
	0x8284, 0x8684, 0x8a84, 0x8e84, 0x9284, 0x9684, 0x9a84, 0x9e84,
	0xa284, 0xa684, 0xaa84, 0xae84, 0xb284, 0xb684, 0xba84, 0xbe84,
	0xc184, 0xc384, 0xc584, 0xc784, 0xc984, 0xcb84, 0xcd84, 0xcf84,
	0xd184, 0xd384, 0xd584, 0xd784, 0xd984, 0xdb84, 0xdd84, 0xdf84,
	0xe104, 0xe204, 0xe304, 0xe404, 0xe504, 0xe604, 0xe704, 0xe804,
	0xe904, 0xea04, 0xeb04, 0xec04, 0xed04, 0xee04, 0xef04, 0xf004,
	0xf0c4, 0xf144, 0xf1c4, 0xf244, 0xf2c4, 0xf344, 0xf3c4, 0xf444,
	0xf4c4, 0xf544, 0xf5c4, 0xf644, 0xf6c4, 0xf744, 0xf7c4, 0xf844,
	0xf8a4, 0xf8e4, 0xf924, 0xf964, 0xf9a4, 0xf9e4, 0xfa24, 0xfa64,
	0xfaa4, 0xfae4, 0xfb24, 0xfb64, 0xfba4, 0xfbe4, 0xfc24, 0xfc64,
	0xfc94, 0xfcb4, 0xfcd4, 0xfcf4, 0xfd14, 0xfd34, 0xfd54, 0xfd74,
	0xfd94, 0xfdb4, 0xfdd4, 0xfdf4, 0xfe14, 0xfe34, 0xfe54, 0xfe74,
	0xfe8c, 0xfe9c, 0xfeac, 0xfebc, 0xfecc, 0xfedc, 0xfeec, 0xfefc,
	0xff0c, 0xff1c, 0xff2c, 0xff3c, 0xff4c, 0xff5c, 0xff6c, 0xff7c,
	0xff88, 0xff90, 0xff98, 0xffa0, 0xffa8, 0xffb0, 0xffb8, 0xffc0,
	0xffc8, 0xffd0, 0xffd8, 0xffe0, 0xffe8, 0xfff0, 0xfff8, 0x0000,
	0x7d7c, 0x797c, 0x757c, 0x717c, 0x6d7c, 0x697c, 0x657c, 0x617c,
	0x5d7c, 0x597c, 0x557c, 0x517c, 0x4d7c, 0x497c, 0x457c, 0x417c,
	0x3e7c, 0x3c7c, 0x3a7c, 0x387c, 0x367c, 0x347c, 0x327c, 0x307c,
	0x2e7c, 0x2c7c, 0x2a7c, 0x287c, 0x267c, 0x247c, 0x227c, 0x207c,
	0x1efc, 0x1dfc, 0x1cfc, 0x1bfc, 0x1afc, 0x19fc, 0x18fc, 0x17fc,
	0x16fc, 0x15fc, 0x14fc, 0x13fc, 0x12fc, 0x11fc, 0x10fc, 0x0ffc,
	0x0f3c, 0x0ebc, 0x0e3c, 0x0dbc, 0x0d3c, 0x0cbc, 0x0c3c, 0x0bbc,
	0x0b3c, 0x0abc, 0x0a3c, 0x09bc, 0x093c, 0x08bc, 0x083c, 0x07bc,
	0x075c, 0x071c, 0x06dc, 0x069c, 0x065c, 0x061c, 0x05dc, 0x059c,
	0x055c, 0x051c, 0x04dc, 0x049c, 0x045c, 0x041c, 0x03dc, 0x039c,
	0x036c, 0x034c, 0x032c, 0x030c, 0x02ec, 0x02cc, 0x02ac, 0x028c,
	0x026c, 0x024c, 0x022c, 0x020c, 0x01ec, 0x01cc, 0x01ac, 0x018c,
	0x0174, 0x0164, 0x0154, 0x0144, 0x0134, 0x0124, 0x0114, 0x0104,
	0x00f4, 0x00e4, 0x00d4, 0x00c4, 0x00b4, 0x00a4, 0x0094, 0x0084,
	0x0078, 0x0070, 0x0068, 0x0060, 0x0058, 0x0050, 0x0048, 0x0040,
	0x0038, 0x0030, 0x0028, 0x0020, 0x0018, 0x0010, 0x0008, 0x0000
};

/* alaw -> signed 16-bit */
static short isdn_audio_alaw_to_s16[] =
{
	0x13fc, 0xec04, 0x0144, 0xfebc, 0x517c, 0xae84, 0x051c, 0xfae4,
	0x0a3c, 0xf5c4, 0x0048, 0xffb8, 0x287c, 0xd784, 0x028c, 0xfd74,
	0x1bfc, 0xe404, 0x01cc, 0xfe34, 0x717c, 0x8e84, 0x071c, 0xf8e4,
	0x0e3c, 0xf1c4, 0x00c4, 0xff3c, 0x387c, 0xc784, 0x039c, 0xfc64,
	0x0ffc, 0xf004, 0x0104, 0xfefc, 0x417c, 0xbe84, 0x041c, 0xfbe4,
	0x083c, 0xf7c4, 0x0008, 0xfff8, 0x207c, 0xdf84, 0x020c, 0xfdf4,
	0x17fc, 0xe804, 0x018c, 0xfe74, 0x617c, 0x9e84, 0x061c, 0xf9e4,
	0x0c3c, 0xf3c4, 0x0084, 0xff7c, 0x307c, 0xcf84, 0x030c, 0xfcf4,
	0x15fc, 0xea04, 0x0164, 0xfe9c, 0x597c, 0xa684, 0x059c, 0xfa64,
	0x0b3c, 0xf4c4, 0x0068, 0xff98, 0x2c7c, 0xd384, 0x02cc, 0xfd34,
	0x1dfc, 0xe204, 0x01ec, 0xfe14, 0x797c, 0x8684, 0x07bc, 0xf844,
	0x0f3c, 0xf0c4, 0x00e4, 0xff1c, 0x3c7c, 0xc384, 0x03dc, 0xfc24,
	0x11fc, 0xee04, 0x0124, 0xfedc, 0x497c, 0xb684, 0x049c, 0xfb64,
	0x093c, 0xf6c4, 0x0028, 0xffd8, 0x247c, 0xdb84, 0x024c, 0xfdb4,
	0x19fc, 0xe604, 0x01ac, 0xfe54, 0x697c, 0x9684, 0x069c, 0xf964,
	0x0d3c, 0xf2c4, 0x00a4, 0xff5c, 0x347c, 0xcb84, 0x034c, 0xfcb4,
	0x12fc, 0xed04, 0x0134, 0xfecc, 0x4d7c, 0xb284, 0x04dc, 0xfb24,
	0x09bc, 0xf644, 0x0038, 0xffc8, 0x267c, 0xd984, 0x026c, 0xfd94,
	0x1afc, 0xe504, 0x01ac, 0xfe54, 0x6d7c, 0x9284, 0x06dc, 0xf924,
	0x0dbc, 0xf244, 0x00b4, 0xff4c, 0x367c, 0xc984, 0x036c, 0xfc94,
	0x0f3c, 0xf0c4, 0x00f4, 0xff0c, 0x3e7c, 0xc184, 0x03dc, 0xfc24,
	0x07bc, 0xf844, 0x0008, 0xfff8, 0x1efc, 0xe104, 0x01ec, 0xfe14,
	0x16fc, 0xe904, 0x0174, 0xfe8c, 0x5d7c, 0xa284, 0x05dc, 0xfa24,
	0x0bbc, 0xf444, 0x0078, 0xff88, 0x2e7c, 0xd184, 0x02ec, 0xfd14,
	0x14fc, 0xeb04, 0x0154, 0xfeac, 0x557c, 0xaa84, 0x055c, 0xfaa4,
	0x0abc, 0xf544, 0x0058, 0xffa8, 0x2a7c, 0xd584, 0x02ac, 0xfd54,
	0x1cfc, 0xe304, 0x01cc, 0xfe34, 0x757c, 0x8a84, 0x075c, 0xf8a4,
	0x0ebc, 0xf144, 0x00d4, 0xff2c, 0x3a7c, 0xc584, 0x039c, 0xfc64,
	0x10fc, 0xef04, 0x0114, 0xfeec, 0x457c, 0xba84, 0x045c, 0xfba4,
	0x08bc, 0xf744, 0x0018, 0xffe8, 0x227c, 0xdd84, 0x022c, 0xfdd4,
	0x18fc, 0xe704, 0x018c, 0xfe74, 0x657c, 0x9a84, 0x065c, 0xf9a4,
	0x0cbc, 0xf344, 0x0094, 0xff6c, 0x327c, 0xcd84, 0x032c, 0xfcd4
};


unsigned char encode_isdn(short sample, char law)
{
	int best=-1;
	int i,diff;
	int best_diff;

	i=0;
	while(i<256)
	{
		diff = (law=='u')?isdn_audio_ulaw_to_s16[i]:isdn_audio_alaw_to_s16[i]-sample;
//printf("s16=%d sample%d diff=%d\n",isdn_audio_to_s16[i],sample,diff);
		if (diff<0)
			diff=0-diff;
//printf("diff=%d\n",diff);

		if (diff<best_diff || best<0)
		{
//printf("better %d\n",i);
			best_diff=diff;
			best=i;
		}
		i++;
	}
	return(best);
}


void write_tone(FILE *fp,double t1,double t2,int length,int fade_in,int fade_out, char law)
{
	double x,s,t,fade;
	int i;

	i=0;
	while(i<length)
	{
		if (i < fade_in)
			fade=(double)i / (double)fade_in;
		else	fade=1.0;
		if (length-1-i < fade_out)
			fade=((double)length-1.0-(double)i) / (double)fade_out;

		s = (double)i / 8000.0 * (double)t1 * 3.1415927F * 2.0;
		t = (double)i / 8000.0 * (double)t2 * 3.1415927F * 2.0;
		x = sin(s)+sin(t);
//printf("%e,%e,%e\n",s,t,x);
		fputc(encode_isdn((short)(x * 6000.0 * fade), law),fp);
		i++;
	}
}

struct fmt {
	unsigned short	stereo; /* 1 = pcm, 2 = adpcm */
	unsigned short	channels; /* number of channels */
	unsigned int	sample_rate; /* sample rate */
	unsigned int	data_rate; /* data rate */
	unsigned short	bytes_sample; /* bytes per sample (all channels) */
	unsigned short	bits_sample; /* bits per sample (one channel) */
};

void write_wav(FILE *fp, char *wav, char law)
{
	unsigned char buffer[256];
	struct fmt *fmt;
	FILE *wfp;
	signed int i;
	int channels, bytes;
	short sample, sample2;
	signed int size, chunk;
	int gotfmt = 0, gotdata = 0;

	if ((wfp=fopen(wav,"r")))
	{
		fread(buffer,8,1,wfp);
		size=(buffer[4]) + (buffer[5]<<8) + (buffer[6]<<16) + (buffer[7]<<24);
		if (!!strncmp((char *)buffer, "RIFF", 4))
		{
			fclose(wfp);
			fprintf(stderr, "Error: %s is no riff file!\n", wav);
			return;
		}
		printf("%c%c%c%c size=%ld\n",buffer[0],buffer[1],buffer[2],buffer[3],size);
		fread(buffer,4,1,wfp);
		size -= 4;
		if (!!strncmp((char *)buffer, "WAVE", 4))
		{
			fclose(wfp);
			fprintf(stderr, "Error: %s is no wave file!\n", wav);
			return;
		}
		while(size > 0)
		{
			if (size>0 && size<8)
			{
				fclose(wfp);
				fprintf(stderr, "Error: Remaining file size %ld not large enough for next chunk.\n",size);
				return;
			}
			fread(buffer,8,1,wfp);
			chunk=(buffer[4]) + (buffer[5]<<8) + (buffer[6]<<16) + (buffer[7]<<24);
//printf("DEBUG: size(%ld) - (8+chunk(%ld) = size(%ld)\n", size, chunk, size-chunk-8);
			size -= (8+chunk);
			if (size < 0)
			{
				fclose(wfp);
				fprintf(stderr, "Error: Chunk '%c%c%c%c' is larger than remainig file size (length=%ld)\n",buffer[0],buffer[1],buffer[2],buffer[3], chunk);
				return;
			}
//			printf("%c%c%c%c lenght=%d\n",buffer[0],buffer[1],buffer[2],buffer[3],chunk);
			if (!strncmp((char *)buffer, "fmt ", 4))
			{
				if (chunk != 16)
				{
					fclose(wfp);
					fprintf(stderr, "Error: Fmt chunk illegal size.\n");
					return;
				}
				fread(buffer, chunk, 1, wfp);
				fmt = (struct fmt *)buffer;
				if (fmt->channels<1 || fmt->channels>2)
				{
					fclose(wfp);
					fprintf(stderr, "Error: Only support one or two channels file.\n");
					return;
				}
				channels = fmt->channels;
				printf("Channels: %d\n", channels);
				if (fmt->sample_rate != 8000)
				{
					fprintf(stderr, "Warning: File has sample rate of %ld.\n", fmt->sample_rate);
				}
				printf("Sample Rate: %ld\n", fmt->sample_rate);
				if (fmt->bits_sample!=8 && fmt->bits_sample!=16)
				{
					fclose(wfp);
					fprintf(stderr, "Error: File has neigher 8 nor 16 bit samples.\n");
					return;
				}
				bytes = (fmt->bits_sample==16)?2:1;
				printf("Bit-Resolution: %d\n", bytes*16-16);
				gotfmt = 1;
			} else
			if (!strncmp((char *)buffer, "data", 4))
			{
				if (!gotfmt)
				{
					fclose(wfp);
					fprintf(stderr, "Error: No fmt chunk fount in file.\n");
					return;
				}
				printf("Length: %ld samples (%ld.%03ld seconds)\n", chunk/bytes/channels, chunk/bytes/channels/8000, ((chunk/bytes/channels)%8000)*1000/8000);
				i=0;
				if (bytes==2 && channels==1)
				{
					while(i<chunk)
					{
						fread(buffer, 2, 1, wfp);
						sample=(buffer[1]<<8) + (buffer[0]);
						fputc(encode_isdn(sample, law),fp);
						i+=2;
					}
				}
				if (bytes==2 && channels==2)
				{
					while(i<chunk)
					{
						fread(buffer, 4, 1, wfp);
						sample=(buffer[1]<<8) + (buffer[0]);
						sample2=(buffer[3]<<8) + (buffer[2]);
						sample = (sample/2) + (sample2/2);
						fputc(encode_isdn(sample, law),fp);
						i+=4;
					}
				}
				if (bytes==1 && channels==1)
				{
					while(i<chunk)
					{
						fread(buffer, 1, 1, wfp);
						sample=(buffer[0]<<8);
						fputc(encode_isdn(sample, law),fp);
						i+=1;
					}
				}
				if (bytes==1 && channels==2)
				{
					while(i<chunk)
					{
						fread(buffer, 2, 1, wfp);
						sample=(buffer[0]<<8);
						sample2=(buffer[1]<<8);
						sample = (sample/2) + (sample2/2);
						fputc(encode_isdn(sample, law),fp);
						i+=2;
					}
				}
				gotdata = 1;
			} else
			{
				printf("Ignoring chunk '%c%c%c%c' (length=%ld)\n",buffer[0],buffer[1],buffer[2],buffer[3], chunk);
				while(chunk > (signed int)sizeof(buffer))
				{
					fread(buffer, sizeof(buffer), 1, wfp);
					chunk -=  sizeof(buffer);
				}
				if (chunk)
					fread(buffer, chunk, 1, wfp);
			}
			
		}
		fclose(wfp);
		if (!gotdata)
		{
			fprintf(stderr, "Error: No data chunk fount in file.\n");
			return;
		}
	}
}

int main(int argc, char *argv[])
{
	FILE *fp;

	if (argc <= 1)
	{
		usage:
		printf("Usage:\n");
		printf("%s wave2alaw <wav file> <alaw file>\n", argv[0]);
		printf("%s wave2ulaw <wav file> <ulaw file>\n", argv[0]);
		printf("%s tone2alaw <frq1> <frq2> <length> <fade in> <fade out> <alaw file>\n", argv[0]);
		printf("%s tone2ulaw <frq1> <frq2> <length> <fade in> <fade out> <ulaw file>\n", argv[0]);
		printf("Length and fade lengths must be given in samples (8000 samples are one second).\n");
		printf("Tones will append to existing files, wav files don't.\n");
		printf("Frequencies may be given as floating point values.\n");
		return(0);
	}

	if (!strcmp(argv[1], "wave2alaw"))
	{
		if (argc <= 3)
			goto usage;
		if ((fp=fopen(argv[3],"w")))
		{
			write_wav(fp,argv[2],'a');
			fclose(fp);
		} else
		{
			printf("Cannot open isdn file %s\n",argv[3]);
		}
	} else
	if (!strcmp(argv[1], "wave2ulaw"))
	{
		if (argc <= 3)
			goto usage;
		if ((fp=fopen(argv[3],"w")))
		{
			write_wav(fp,argv[2],'u');
			fclose(fp);
		} else
		{
			printf("Cannot open isdn file %s\n",argv[3]);
		}
	} else
	if (!strcmp(argv[1], "tone2alaw"))
	{
		if (argc <= 7)
			goto usage;
		if ((fp=fopen(argv[7],"a")))
		{
			write_tone(fp,strtod(argv[2],NULL),strtod(argv[3],NULL),atoi(argv[4]),atoi(argv[5]),atoi(argv[6]),'a');
			fclose(fp);
		} else
		{
			printf("Cannot open isdn file %s\n",argv[7]);
		}

	} else
	if (!strcmp(argv[1], "tone2ulaw"))
	{
		if (argc <= 7)
			goto usage;
		if ((fp=fopen(argv[7],"a")))
		{
			write_tone(fp,atoi(argv[2]),atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),atoi(argv[6]),'u');
			fclose(fp);
		} else
		{
			printf("Cannot open isdn file %s\n",argv[7]);
		}
	} else
		goto usage;

	return(0);
}
