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
static inline void scpy(char *dst, const char *src, unsigned int siz)
{
	strncpy(dst, src, siz);
	dst[siz-1] = '\0';
}

/* safe strcat/strncat */

#define SCAT(dst, src) scat(dst, src, sizeof(dst))
static inline void scat(char *dst, const char *src, unsigned int siz)
{
	strncat(dst, src, siz-strlen(dst)-1);
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
static inline void sprint(char *dst, unsigned int siz, const char *fmt, ...)
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

#define FATAL(fmt, arg...) _fatal(__FILE__, __FUNCTION__, __LINE__, fmt, ##arg)
/* fatal error with error message and exit */
static inline void _fatal(const char *file, const char *function, int line, const char *fmt, ...)
{
	va_list args;
	char buffer[256];

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	buffer[sizeof(buffer)-1] = '\0';
	fprintf(stderr, "FATAL ERROR in function %s/%s, line %d: %s", file, function, line, buffer);
	fprintf(stderr, "This error is not recoverable, must exit here.\n");
#ifdef DEBUG_FUNC
	debug(file, function, line, "FATAL", buffer);
	debug(file, function, line, "FATAL", (char *)"This error is not recoverable, must exit here.\n");
#endif
	exit(EXIT_FAILURE);
}

/* memory allocation with setting to zero */
#define MALLOC(size) _malloc(size, __FILE__, __FUNCTION__, __LINE__)
static inline void *_malloc(unsigned int size, const char *file, const char *function, int line)
{
	void *addr;
	addr = malloc(size);
	if (!addr)
		_fatal(file, function, line, "No memory for %d bytes.\n", size);
	memset(addr, 0, size);
	return addr;
}

/* memory freeing with clearing memory to prevent using freed memory */
#define FREE(addr, size) _free(addr, size)
static inline void _free(void *addr, int size)
{
	if (size)
		memset(addr, 0, size);
	free(addr);
}

/* fill buffer and be sure that it's result is 0-terminated, also remove newline */
#define GETLINE(buffer, fp) _getline(buffer, sizeof(buffer), fp)
static inline char *_getline(char *buffer, int size, FILE *fp)
{
	if (!fgets(buffer, size-1, fp))
		return NULL;
	buffer[size-1] = '\0';
	if (!buffer[0])
		return buffer;
	if (buffer[strlen(buffer)-1] == '\n')
		buffer[strlen(buffer)-1] = '\0';
	if (buffer[strlen(buffer)-1] == '\r')
		buffer[strlen(buffer)-1] = '\0';
	return buffer;
}


