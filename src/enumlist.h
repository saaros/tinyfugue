/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: enumlist.h,v 35000.2 1997/03/27 01:04:24 hawkeye Exp $ */

/* It may not be easy to read, but it keeps the constants and the array in the
 * same place, so they can't get out of sync.
 */

bicode(enum {,     static CONST char *enum_bamf[] = {)
bicode(BAMF_OFF,   "off"),
bicode(BAMF_UNTER, "on"),
bicode(BAMF_OLD,   "old"),
bicode(BAMF_COUNT, NULL)
};

bicode(enum {,      static CONST char *enum_clock[] = {)
bicode(CLOCK_OFF,   "off"),
bicode(CLOCK_12,    "12-hour"),
bicode(CLOCK_24,    "24-hour"),
bicode(CLOCK_COUNT, NULL)
};

bicode(enum {,          static CONST char *enum_emul[] = {)
bicode(EMUL_RAW,        "raw"),
bicode(EMUL_PRINT,      "print"),
bicode(EMUL_ANSI_STRIP, "ansi_strip"),
bicode(EMUL_ANSI_ATTR,  "ansi_attr"),
bicode(EMUL_DEBUG,      "debug"),
bicode(EMUL_COUNT,      NULL)
};

bicode(enum {,       CONST char *enum_match[] = {)
bicode(MATCH_SIMPLE, "simple"),
bicode(MATCH_GLOB,   "glob"),
bicode(MATCH_REGEXP, "regexp"),
bicode(MATCH_COUNT,  NULL)
};

