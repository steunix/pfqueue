
#ifndef __FE_PFQTCP_H
#define __FE_PFQTCP_H

#include "pfqmessage.h"

/* Protocol commands */

#define	CMD_NUMMSG	"NUMMSG"
#define	CMD_NUMQ	"NUMQ"
#define	CMD_QNAME	"QNAME"
#define CMD_SETQ	"SETQ"
#define CMD_LSTIDS	"LSTIDS"
#define CMD_PATH	"MSGPATH"
#define CMD_FROM	"MSGFROM"
#define CMD_TO		"MSGTO"
#define CMD_SUBJ	"MSGSUBJ"
#define CMD_MSGID	"MSGID"
#define CMD_STATUS	"MSGSTAT"
#define CMD_QUIT	"QUIT"
#define CMD_BODY	"BODY"
#define CMD_LASTCHG	"LASTC"

#define CMD_MSGDEL	"MSGDEL"
#define	CMD_MSGREQ	"MSGREQ"
#define CMD_MSGREL	"MSGREL"
#define CMD_MSGHOLD	"MSGHOLD"

#define CMD_REPLY_LEN	8
#define CMD_REPLY	"RESULT"
#define CMD_ERROR	"ERROR-"
#endif
