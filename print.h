/*
 *	print.h - line oriented output through DOS _PRINT
 */

#ifndef PRINT_H
#define PRINT_H

#include "usb.h"

void	pchar(char c);
void	pstr(const char *s);
void	pdec(u32 v);
void	phex(u32 v, int digits);
void	pflush(void);			/* append CR/LF and print the line */

#endif /* PRINT_H */
