/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1996 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: enumlist.h,v 35000.8 1999/01/31 00:27:40 hawkeye Exp $ */

/* It may not be easy to read, but it keeps the constants and the array in the
 * same place, so they can't get out of sync.
 */

#ifndef ENUMEXTERN
# define ENUMEXTERN
#endif

bicode(enum,       static CONST char *enum_bamf[] = )
{
bicode(BAMF_OFF,   "off"),
bicode(BAMF_UNTER, "on"),
bicode(BAMF_OLD,   "old"),
bicode(BAMF_COUNT, NULL)
};

bicode(enum,            static CONST char *enum_emul[] = )
{
bicode(EMUL_RAW,        "raw"),
bicode(EMUL_PRINT,      "print"),
bicode(EMUL_ANSI_STRIP, "ansi_strip"),
bicode(EMUL_ANSI_ATTR,  "ansi_attr"),
bicode(EMUL_DEBUG,      "debug"),
bicode(EMUL_COUNT,      NULL)
};

ENUMEXTERN CONST char *enum_match[]
bicode(; enum,       = )
{
bicode(MATCH_SIMPLE, "simple"),
bicode(MATCH_GLOB,   "glob"),
bicode(MATCH_REGEXP, "regexp"),
bicode(MATCH_COUNT,  NULL)
};

ENUMEXTERN CONST char *enum_status[]
bicode(; enum,       = )
{
bicode(STAT_MORE,    "more"),
bicode(STAT_WORLD,   "world"),
bicode(STAT_READ,    "read"),
bicode(STAT_ACTIVE,  "active"),
bicode(STAT_LOGGING, "log"),
bicode(STAT_MAIL,    "mail"),
bicode(STAT_CLOCK,   "clock"),
bicode(STAT_COUNT,   NULL)
};

ENUMEXTERN CONST char *enum_eol[]
bicode(; enum,    = )
{
bicode(EOL_LF,    "LF"),
bicode(EOL_CR,    "CR"),
bicode(EOL_CRLF,  "CRLF"),
bicode(EOL_COUNT, NULL)
};

#undef ENUMEXTERN
#undef bicode
