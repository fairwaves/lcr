/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Macros to do safe string operations to avoid buffer overflows             **
** Macros for memory allocation, feeing and error handling                   **
**                                                                           **
\*****************************************************************************/ 


/* safe strcpy/strncpy */

#define SCPY(dst, src) scpy(dst, src, sizeof(dst))
static inline void scpy(char *dst, char *src, unsigned int siz)
{
	strncpy(dst, src, siz);
	dst[siz-1] = '\0';
}

/* safe strcat/strncat */

#define SCAT(dst, src) scat(dst, src, sizeof(dst))
static inline void scat(char *dst, char *src, unsigned int siz)
{
	strncat(dst, src, siz);
	dst[siz-1] = '\0';
}

/* safe concat of a byte */

#define SCCAT(dst, src) sccat(dst, src, sizeof(dst))
static inline void sccat(char *dst, char chr, unsigned int siz)
{
	if (strlen(dst) < siz-1)
	{
		dst[strlen(dst)+1] = '\0';
		dst[strlen(dst)] = chr;
	}
}

/* safe sprintf/snprintf */

#define SPRINT(dst, fmt, arg...) sprint(dst, sizeof(dst), fmt, ## arg)
static inline void sprint(char *dst, unsigned int siz, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(dst, siz, fmt, args);
	dst[siz-1] = '\0';
	va_end(args);
}

/* unsafe */
#define UCPY strcpy
#define UNCPY strncpy
#define UCAT strcat
#define UNCAT strncat
#define UPRINT sprintf
#define UNPRINT snprintf
#define VUNPRINT vsnprintf

/* fatal error with error message and exit */
#define FATAL(fmt, arg...) fatal(__FUNCTION__, __LINE__, fmt, ##arg)
static inline void fatal(const char *function, int line, char *fmt, ...)
{
	va_list args;
	char buffer[256];

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	buffer[sizeof(buffer)-1] = '\0';
	fprintf(stderr, "FATAL ERROR in function %s, line %d: %s", function, line, buffer);
	fprintf(stderr, "This error is not recoverable, must exit here.\n");
#ifdef DEBUG_FUNC
	debug(function, line, "FATAL ERROR", buffer);
	debug(function, line, "FATAL ERROR", "This error is not recoverable, must exit here.\n");
#endif
	exit(EXIT_FAILURE);
}

/* memory allocation with setting to zero */
#define MALLOC(size) _malloc(size, __FUNCTION__, __LINE__)
static inline void *_malloc(unsigned int size, const char *function, int line)
{
	void *addr;
	addr = malloc(size);
	if (!addr)
		fatal(function, line, "No memory for %d bytes.\n", size);
	memset(addr, 0, size);
	return(addr);
}

/* memory freeing with clearing memory to prevent using freed memory */
#define FREE(addr, size) _free(addr, size)
static inline void _free(void *addr, int size)
{
	if (size)
		memset(addr, 0, size);
	free(addr);
}


