/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: enumlist.h,v 35000.4 1997/11/13 07:40:10 hawkeye Exp $ */

/* It may not be easy to read, but it keeps the constants and the array in the
 * same place, so they can't get out of sync.
 */

bicode(enum {,     static CONST char *enum_bamf[] = {)
bicode(BAMF_OFF,   "off"),
bicode(BAMF_UNTER, "on"),
bicode(BAMF_OLD,   "old"),
bicode(BAMF_COUNT, NULL)
};

bicode(enum {,          static CONST char *enum_emul[] = {)
bicode(EMUL_RAW,        "raw"),
bicode(EMUL_PRINT,      "print"),
bicode(EMUL_ANSI_STRIP, "ansi_strip"),
bicode(EMUL_ANSI_ATTR,  "ansi_attr"),
bicode(EMUL_DEBUG,      "debug"),
bicode(EMUL_COUNT,      NULL)
};

bicode(extern,       /**/)
CONST char *enum_match[]
bicode(; enum {,     = {)
bicode(MATCH_SIMPLE, "simple"),
bicode(MATCH_GLOB,   "glob"),
bicode(MATCH_REGEXP, "regexp"),
bicode(MATCH_COUNT,  NULL)
};

bicode(extern,       /**/)
CONST char *enum_status[]
bicode(; enum {,     = {)
bicode(STAT_MORE,    "more"),
bicode(STAT_WORLD,   "world"),
bicode(STAT_READ,    "read"),
bicode(STAT_ACTIVE,  "active"),
bicode(STAT_LOGGING, "log"),
bicode(STAT_MAIL,    "mail"),
bicode(STAT_CLOCK,   "clock"),
bicode(STAT_COUNT,   NULL)
};
