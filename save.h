/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Macros to do save string operations to avoid buffer overflows.            **
**                                                                           **
\*****************************************************************************/ 


/* save strcpy/strncpy */

#define SCPY(dst, src) scpy(dst, src, sizeof(dst))
extern __inline__ void scpy(char *dst, char *src, unsigned int siz)
{
	strncpy(dst, src, siz);
	dst[siz-1] = '\0';
}

/* save strcat/strncat */

#define SCAT(dst, src) scat(dst, src, sizeof(dst))
extern __inline__ void scat(char *dst, char *src, unsigned int siz)
{
	strncat(dst, src, siz);
	dst[siz-1] = '\0';
}

/* save concat of a byte */

#define SCCAT(dst, src) sccat(dst, src, sizeof(dst))
extern __inline__ void sccat(char *dst, char chr, unsigned int siz)
{
	if (strlen(dst) < siz-1)
	{
		dst[strlen(dst)+1] = '\0';
		dst[strlen(dst)] = chr;
	}
}

/* save sprintf/snprintf */

#define SPRINT(dst, fmt, arg...) sprint(dst, sizeof(dst), fmt, ## arg)
extern __inline__ void sprint(char *dst, unsigned int siz, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(dst, siz, fmt, args);
	dst[siz-1] = '\0';
	va_end(args);
}

/* unsave */
#define UCPY strcpy
#define UNCPY strncpy
#define UCAT strcat
#define UNCAT strncat
#define UPRINT sprintf
#define UNPRINT snprintf
#define VUNPRINT vsnprintf

