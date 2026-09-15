/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Dave Borman at Cray Research, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is adapted from FreeBSD libc's setmode.c for Panthera userland
 * tools that need BSD mode parsing without depending on libc-private headers.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#define SET_LEN 6
#define SET_LEN_INCR 4

typedef struct bitcmd {
	char cmd;
	char cmd2;
	mode_t bits;
} BITCMD;

#define CMD2_CLR 0x01
#define CMD2_SET 0x02
#define CMD2_GBITS 0x04
#define CMD2_OBITS 0x08
#define CMD2_UBITS 0x10

#define STANDARD_BITS (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)

static mode_t get_current_umask(void);
static void *realloc_items(void *, size_t, size_t);
static BITCMD *addcmd(BITCMD *, mode_t, mode_t, mode_t, mode_t);
static void compress_mode(BITCMD *);

mode_t
getmode(const void *bbox, mode_t omode)
{
	const BITCMD *set;
	mode_t clrval, newmode, value;

	set = (const BITCMD *)bbox;
	newmode = omode;
	for (value = 0;; set++) {
		switch (set->cmd) {
		case 'u':
			value = (newmode & S_IRWXU) >> 6;
			goto common;
		case 'g':
			value = (newmode & S_IRWXG) >> 3;
			goto common;
		case 'o':
			value = newmode & S_IRWXO;
common:
			if (set->cmd2 & CMD2_CLR) {
				clrval = (set->cmd2 & CMD2_SET) ? S_IRWXO : value;
				if (set->cmd2 & CMD2_UBITS)
					newmode &= ~((clrval << 6) & set->bits);
				if (set->cmd2 & CMD2_GBITS)
					newmode &= ~((clrval << 3) & set->bits);
				if (set->cmd2 & CMD2_OBITS)
					newmode &= ~(clrval & set->bits);
			}
			if (set->cmd2 & CMD2_SET) {
				if (set->cmd2 & CMD2_UBITS)
					newmode |= (value << 6) & set->bits;
				if (set->cmd2 & CMD2_GBITS)
					newmode |= (value << 3) & set->bits;
				if (set->cmd2 & CMD2_OBITS)
					newmode |= value & set->bits;
			}
			break;
		case '+':
			newmode |= set->bits;
			break;
		case '-':
			newmode &= ~set->bits;
			break;
		case 'X':
			if (omode & (S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH))
				newmode |= set->bits;
			break;
		case '\0':
		default:
			return newmode;
		}
	}
}

#define ADDCMD(a, b, c, d)                                                \
	do {                                                              \
		if (set >= endset) {                                      \
			BITCMD *newset;                                   \
			setlen += SET_LEN_INCR;                           \
			newset = realloc_items(saveset, setlen, sizeof(BITCMD)); \
			if (newset == NULL)                               \
				goto out;                                 \
			set = newset + (set - saveset);                    \
			saveset = newset;                                  \
			endset = newset + (setlen - 2);                    \
		}                                                         \
		set = addcmd(set, (mode_t)(a), (mode_t)(b), (mode_t)(c), (d)); \
	} while (0)

void *
setmode(const char *p)
{
	int serrno;
	char op, *ep;
	BITCMD *set, *saveset, *endset;
	mode_t mask, perm, permXbits, who;
	long perml;
	int equalopdone;
	unsigned int setlen;

	if (!*p) {
		errno = EINVAL;
		return NULL;
	}

	mask = ~get_current_umask();
	setlen = SET_LEN + 2;

	if ((set = malloc(setlen * sizeof(BITCMD))) == NULL)
		return NULL;
	saveset = set;
	endset = set + (setlen - 2);

	if (isdigit((unsigned char)*p)) {
		errno = 0;
		perml = strtol(p, &ep, 8);
		if (*ep) {
			errno = EINVAL;
			goto out;
		}
		if (errno == ERANGE && (perml == LONG_MAX || perml == LONG_MIN))
			goto out;
		if (perml & ~(STANDARD_BITS | S_ISTXT)) {
			errno = EINVAL;
			goto out;
		}
		perm = (mode_t)perml;
		ADDCMD('=', (STANDARD_BITS | S_ISTXT), perm, mask);
		set->cmd = 0;
		return saveset;
	}

	equalopdone = 0;
	for (;;) {
		for (who = 0;; ++p) {
			switch (*p) {
			case 'a':
				who |= STANDARD_BITS;
				break;
			case 'u':
				who |= S_ISUID | S_IRWXU;
				break;
			case 'g':
				who |= S_ISGID | S_IRWXG;
				break;
			case 'o':
				who |= S_IRWXO;
				break;
			default:
				goto getop;
			}
		}

getop:
		if ((op = *p++) != '+' && op != '-' && op != '=') {
			errno = EINVAL;
			goto out;
		}
		if (op == '=')
			equalopdone = 0;

		who &= ~S_ISTXT;
		for (perm = 0, permXbits = 0;; ++p) {
			switch (*p) {
			case 'r':
				perm |= S_IRUSR | S_IRGRP | S_IROTH;
				break;
			case 's':
				if (!who || who & ~S_IRWXO)
					perm |= S_ISUID | S_ISGID;
				break;
			case 't':
				if (!who || who & ~S_IRWXO) {
					who |= S_ISTXT;
					perm |= S_ISTXT;
				}
				break;
			case 'w':
				perm |= S_IWUSR | S_IWGRP | S_IWOTH;
				break;
			case 'X':
				permXbits = S_IXUSR | S_IXGRP | S_IXOTH;
				break;
			case 'x':
				perm |= S_IXUSR | S_IXGRP | S_IXOTH;
				break;
			case 'u':
			case 'g':
			case 'o':
				if (perm) {
					ADDCMD(op, who, perm, mask);
					perm = 0;
				}
				if (op == '=')
					equalopdone = 1;
				if (op == '+' && permXbits) {
					ADDCMD('X', who, permXbits, mask);
					permXbits = 0;
				}
				ADDCMD(*p, who, op, mask);
				break;
			default:
				if (perm || (op == '=' && !equalopdone)) {
					if (op == '=')
						equalopdone = 1;
					ADDCMD(op, who, perm, mask);
					perm = 0;
				}
				if (permXbits) {
					ADDCMD('X', who, permXbits, mask);
					permXbits = 0;
				}
				goto apply;
			}
		}

apply:
		if (!*p)
			break;
		if (*p != ',')
			goto getop;
		++p;
	}
	set->cmd = 0;
	compress_mode(saveset);
	return saveset;

out:
	serrno = errno;
	free(saveset);
	errno = serrno;
	return NULL;
}

static mode_t
get_current_umask(void)
{
	sigset_t sigset, sigoset;
	mode_t mask;

	sigfillset(&sigset);
	(void)sigprocmask(SIG_BLOCK, &sigset, &sigoset);
	(void)umask(mask = umask(0));
	(void)sigprocmask(SIG_SETMASK, &sigoset, NULL);
	return mask;
}

static void *
realloc_items(void *ptr, size_t count, size_t item_size)
{
	if (item_size != 0 && count > SIZE_MAX / item_size) {
		errno = ENOMEM;
		return NULL;
	}
	return realloc(ptr, count * item_size);
}

static BITCMD *
addcmd(BITCMD *set, mode_t op, mode_t who, mode_t oparg, mode_t mask)
{
	switch (op) {
	case '=':
		set->cmd = '-';
		set->cmd2 = 0;
		set->bits = who ? who : STANDARD_BITS;
		set++;
		op = '+';
		/* FALLTHROUGH */
	case '+':
	case '-':
	case 'X':
		set->cmd = (char)op;
		set->cmd2 = 0;
		set->bits = (who ? who : mask) & oparg;
		break;
	case 'u':
	case 'g':
	case 'o':
		set->cmd = (char)op;
		if (who) {
			set->cmd2 = ((who & S_IRUSR) ? CMD2_UBITS : 0) |
			    ((who & S_IRGRP) ? CMD2_GBITS : 0) |
			    ((who & S_IROTH) ? CMD2_OBITS : 0);
			set->bits = (mode_t)~0;
		} else {
			set->cmd2 = CMD2_UBITS | CMD2_GBITS | CMD2_OBITS;
			set->bits = mask;
		}

		if (oparg == '+')
			set->cmd2 |= CMD2_SET;
		else if (oparg == '-')
			set->cmd2 |= CMD2_CLR;
		else if (oparg == '=')
			set->cmd2 |= CMD2_SET | CMD2_CLR;
		break;
	}
	return set + 1;
}

static void
compress_mode(BITCMD *set)
{
	BITCMD *nset;
	int setbits, clrbits, Xbits, op;

	for (nset = set;;) {
		while ((op = nset->cmd) != '+' && op != '-' && op != 'X') {
			*set++ = *nset++;
			if (!op)
				return;
		}

		for (setbits = clrbits = Xbits = 0;; nset++) {
			if ((op = nset->cmd) == '-') {
				clrbits |= nset->bits;
				setbits &= ~nset->bits;
				Xbits &= ~nset->bits;
			} else if (op == '+') {
				setbits |= nset->bits;
				clrbits &= ~nset->bits;
				Xbits &= ~nset->bits;
			} else if (op == 'X') {
				Xbits |= nset->bits & ~setbits;
			} else {
				break;
			}
		}
		if (clrbits) {
			set->cmd = '-';
			set->cmd2 = 0;
			set->bits = clrbits;
			set++;
		}
		if (setbits) {
			set->cmd = '+';
			set->cmd2 = 0;
			set->bits = setbits;
			set++;
		}
		if (Xbits) {
			set->cmd = 'X';
			set->cmd2 = 0;
			set->bits = Xbits;
			set++;
		}
	}
}
