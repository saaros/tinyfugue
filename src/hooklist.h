/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1996 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: hooklist.h,v 35000.6 1999/01/31 00:27:45 hawkeye Exp $ */

/* It may not be easy to read, but it keeps the constants and the array in the
 * same place, so they can't get out of sync.
 */

bicode(H_ACTIVITY,   "ACTIVITY"),
bicode(H_BACKGROUND, "BACKGROUND"),
bicode(H_BAMF,       "BAMF"),
bicode(H_CONFAIL,    "CONFAIL"),
bicode(H_CONFLICT,   "CONFLICT"),
bicode(H_CONNECT,    "CONNECT"),
bicode(H_DISCONNECT, "DISCONNECT"),
bicode(H_KILL,       "KILL"),
bicode(H_LOAD,       "LOAD"),
bicode(H_LOADFAIL,   "LOADFAIL"),
bicode(H_LOG,        "LOG"),
bicode(H_LOGIN,      "LOGIN"),
bicode(H_MAIL,       "MAIL"),
bicode(H_MORE,       "MORE"),
bicode(H_NOMACRO,    "NOMACRO"),
bicode(H_PENDING,    "PENDING"),
bicode(H_PROCESS,    "PROCESS"),
bicode(H_PROMPT,     "PROMPT"),
bicode(H_PROXY,      "PROXY"),
bicode(H_REDEF,      "REDEF"),
bicode(H_RESIZE,     "RESIZE"),
bicode(H_RESUME,     "RESUME"),
bicode(H_SEND,       "SEND"),
bicode(H_SHADOW,     "SHADOW"),
bicode(H_SHELL,      "SHELL"),
bicode(H_SIGHUP,     "SIGHUP"),
bicode(H_SIGTERM,    "SIGTERM"),
bicode(H_SIGUSR1,    "SIGUSR1"),
bicode(H_SIGUSR2,    "SIGUSR2"),
bicode(H_WORLD,      "WORLD"),
bicode(NUM_HOOKS,    NULL)
