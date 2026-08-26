/*
 *	print.c - line oriented output through DOS _PRINT
 *
 *	Lines are collected in a buffer and handed to DOS in one go, which
 *	keeps the number of DOS calls down and works both while a device
 *	driver is initialising and from an ordinary program.
 */

#include "print.h"

static char linebuf[128] = { 0 };
static int linepos = 0;

void pchar(char c)
{
	if (linepos < (int)sizeof(linebuf) - 3)
		linebuf[linepos++] = c;
}

void pstr(const char *s)
{
	while (*s)
		pchar(*s++);
}

void pdec(u32 v)
{
	char tmp[12];
	int n = 0;

	if (!v) {
		pchar('0');
		return;
	}
	while (v) {
		tmp[n++] = (char)('0' + (v % 10));
		v /= 10;
	}
	while (n)
		pchar(tmp[--n]);
}

void phex(u32 v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";

	while (digits--)
		pchar(hex[(v >> (digits * 4)) & 0x0F]);
}

void pflush(void)
{
	linebuf[linepos++] = '\r';
	linebuf[linepos++] = '\n';
	linebuf[linepos] = 0;
	dos_print(linebuf);
	linepos = 0;
}
