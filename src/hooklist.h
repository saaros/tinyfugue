/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: hooklist.h,v 35000.17 2003/10/31 01:40:10 hawkeye Exp $ */

/* This keeps the constants and the array in the same place
 * so they can't get out of sync.
 */

gencode(H_ACTIVITY,   "ACTIVITY",   HT_ALERT | HT_XSOCK),
gencode(H_BACKGROUND, "BACKGROUND", HT_ALERT | HT_XSOCK),
gencode(H_BAMF,       "BAMF",	    HT_WORLD | HT_XSOCK),
gencode(H_CONFAIL,    "CONFAIL",    HT_WORLD | HT_XSOCK),
gencode(H_CONFLICT,   "CONFLICT",   0),
gencode(H_CONNECT,    "CONNECT",    HT_WORLD | HT_XSOCK),
gencode(H_CONNETFAIL, "CONNETFAIL", HT_WORLD | HT_XSOCK),
gencode(H_DISCONNECT, "DISCONNECT", HT_WORLD | HT_XSOCK),
gencode(H_KILL,       "KILL",	    0),
gencode(H_LOAD,       "LOAD",	    0),
gencode(H_LOADFAIL,   "LOADFAIL",   0),
gencode(H_LOG,        "LOG",	    0),
gencode(H_LOGIN,      "LOGIN",	    0),
gencode(H_MAIL,       "MAIL",	    HT_ALERT),
gencode(H_MORE,       "MORE",	    0),
gencode(H_NOMACRO,    "NOMACRO",    0),
gencode(H_PENDING,    "PENDING",    HT_WORLD | HT_XSOCK),
gencode(H_PREACTIVITY,"PREACTIVITY",0),
gencode(H_PROCESS,    "PROCESS",    0),
gencode(H_PROMPT,     "PROMPT",	    0),
gencode(H_PROXY,      "PROXY",	    0),
gencode(H_REDEF,      "REDEF",	    0),
gencode(H_RESIZE,     "RESIZE",	    0),
gencode(H_SEND,       "SEND",	    0),
gencode(H_SHADOW,     "SHADOW",	    0),
gencode(H_SHELL,      "SHELL",	    0),
gencode(H_SIGHUP,     "SIGHUP",	    0),
gencode(H_SIGTERM,    "SIGTERM",    0),
gencode(H_SIGUSR1,    "SIGUSR1",    0),
gencode(H_SIGUSR2,    "SIGUSR2",    0),
gencode(H_WORLD,      "WORLD",	    HT_WORLD),
gencode(NUM_HOOKS,    NULL,	    0)
