/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 2000-2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: opcodes.h,v 35004.17 2003/05/27 01:09:23 hawkeye Exp $ */

/*        label     code  type  arg   result */
/*        -----     ----  ----  ---   ------ */

/* 01-1F expression operators without 1-char tokens */
defopcode(EQUAL    ,0x01, EXPR, INT,  NONE)  /*  ==  */
defopcode(NOTEQ    ,0x02, EXPR, INT,  NONE)  /*  !=  */
defopcode(GTE      ,0x03, EXPR, INT,  NONE)  /*  >=  */
defopcode(LTE      ,0x04, EXPR, INT,  NONE)  /*  <=  */
defopcode(STREQ    ,0x05, EXPR, INT,  NONE)  /*  =~  */
defopcode(STRNEQ   ,0x06, EXPR, INT,  NONE)  /*  !~  */
defopcode(MATCH    ,0x07, EXPR, INT,  NONE)  /*  =/  */
defopcode(NMATCH   ,0x08, EXPR, INT,  NONE)  /*  !/  */
defopcode(ASSIGN   ,0x09, EXPR, INT,  SIDE)  /*  :=  */
defopcode(PREINC   ,0x0A, EXPR, INT,  SIDE)  /*  ++  */
defopcode(PREDEC   ,0x0B, EXPR, INT,  SIDE)  /*  --  */
defopcode(FUNC     ,0x0C, EXPR, INT,  SIDE)  /*  name(...)  */
/* 20-7E: expr ops w/ tokens represented by the corresponding ASCII char */
defopcode(NOT      ,'!',  EXPR, INT,  NONE)
defopcode(MUL      ,'*',  EXPR, INT,  NONE)
defopcode(ADD      ,'+',  EXPR, INT,  NONE)
defopcode(SUB      ,'-',  EXPR, INT,  NONE)
defopcode(DIV      ,'/',  EXPR, INT,  NONE)
defopcode(LT       ,'<',  EXPR, INT,  NONE)
defopcode(GT       ,'>',  EXPR, INT,  NONE)
/* 80-8F substitution operators (append/push pairs) */
defopcode(ASPECIAL ,0x80, SUB,  CHAR, APP)   /* special variable [0*#?R] */
defopcode(PSPECIAL ,0x81, SUB,  CHAR, PUSH)
defopcode(AREG     ,0x82, SUB,  INT,  APP)   /* regexp captured string */
defopcode(PREG     ,0x83, SUB,  INT,  PUSH)
defopcode(APARM    ,0x84, SUB,  INT,  APP)   /* positional param */
defopcode(PPARM    ,0x85, SUB,  INT,  PUSH)
defopcode(ALPARM   ,0x86, SUB,  INT,  APP)   /* pos. param, from end */
defopcode(PLPARM   ,0x87, SUB,  INT,  PUSH)
defopcode(ACMDSUB  ,0x88, SUB,  NONE, APP)   /* output of a cmdsub */
defopcode(PCMDSUB  ,0x89, SUB,  NONE, PUSH)
defopcode(AMAC     ,0x8A, SUB,  STRP, APP)   /* value of macro */
defopcode(PMAC     ,0x8B, SUB,  STRP, PUSH)
defopcode(AVAR     ,0x8C, SUB,  STRP, APP)   /* value of variable */
defopcode(PVAR     ,0x8D, SUB,  STRP, PUSH)
defopcode(PBUF     ,0x8F, SUB,  STRP, PUSH)
/* A0-AF jump operators.  Complementary pairs differ only in last bit. */
defopcode(JZ       ,0xA0, JUMP, INT,  NONE)   /* jump if zero */
defopcode(JNZ      ,0xA1, JUMP, INT,  NONE)   /* jump if not zero */
defopcode(JRZ      ,0xA2, JUMP, INT,  NONE)   /* jump if user_result == 0 */
defopcode(JRNZ     ,0xA3, JUMP, INT,  NONE)   /* jump if user_result != 0 */
defopcode(JEMPTY   ,0xA4, JUMP, INT,  NONE)   /* jump if empty string */
defopcode(JNEMPTY  ,0xA5, JUMP, INT,  NONE)   /* jump if not empty string */
defopcode(JUMP     ,0xAF, JUMP, INT,  NONE)
/* B0-FF control operators */
defopcode(SEND     ,0xB0, CTRL, STRP, NONE)   /* send string to server */
defopcode(EXECUTE  ,0xB1, CTRL, STRP, NONE)   /* execute arbitrary cmd line */
defopcode(BUILTIN  ,0xB2, CTRL, CMDP, TRUE)   /* execute a resovled builtin */
defopcode(COMMAND  ,0xB4, CTRL, CMDP, TRUE)   /* execute a resovled command */
defopcode(MACRO    ,0xB6, CTRL, STRP, TRUE)   /* execute a macro cmd line */
defopcode(ARG      ,0xB8, CTRL, STRP, NONE)   /* arg for BUILTIN or COMMAND */
defopcode(APPEND   ,0xB9, CTRL, STRP, NONE)
defopcode(PUSHBUF  ,0xBA, CTRL, NONE, NONE)
defopcode(POPBUF   ,0xBB, CTRL, NONE, NONE)
defopcode(CMDSUB   ,0xBC, CTRL, NONE, NONE)   /* new frame & tfout queue */
/*defopcode(POPFILE  ,0xBD, CTRL, CHAR, NONE)*/   /* pop pointer to a tfile */
defopcode(PUSH     ,0xC3, CTRL, VALP, NONE)
defopcode(POP      ,0xC4, CTRL, NONE, NONE)
defopcode(DUP      ,0xC5, CTRL, INT,  NONE)
defopcode(RETURN   ,0xC6, CTRL, VALP, NONE)
defopcode(RESULT   ,0xC7, CTRL, VALP, NONE)
defopcode(TEST     ,0xC8, CTRL, VALP, NONE)
defopcode(DONE     ,0xC9, CTRL, NONE, NONE)
defopcode(ENDIF    ,0xCA, CTRL, NONE, NONE)
defopcode(PIPE     ,0xCB, CTRL, NONE, NONE)   /* pipe from this stmt to next */
defopcode(NOP      ,0xCC, CTRL, NONE, NONE)

#undef defopcode
