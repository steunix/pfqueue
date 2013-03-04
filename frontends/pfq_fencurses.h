
#ifndef __FE_NCURSES_H
#define __FE_NCURSES_H

#include "pfqmessage.h"

#include <ncurses.h>

#define MSG_MAX_ROWS	500
#define MSG_MAX_COLS	80

WINDOW *mainwnd;
WINDOW *lwnd;
WINDOW *mwnd, *mwndb;
WINDOW *bwnd, *bwndb;
WINDOW *hwnd;
int    bwnd_height;

char *bline;

int CURROW;	// Current screen row
int CURMSG;	// Current message
int PREVROW;	// Previous row (before change of CURROW)
int PREVMSG;	// Previous message
int FIRSTMSG;	// First msg shown in the list
int FIRSTROW;	// First row for the list
int MAXROW;	// Last row for the list
int COL1, COL2; // From/To columns for queue list

int goto_msg(int);
int goto_next();
int goto_prev();
int goto_first();
int goto_last();
int goto_nextpage();
int goto_prevpage();

extern int CURMSG;

#define CP_HEAD		2
#define CP_MSG		3
#define CP_TAG		4
#define CP_DIALOG	5
#define CP_OPTDIS	6
#define CP_OPTENA	7

struct msg_list_t {
	char id[20];
};

// Interface
int 		fe_init();
void		fe_close();
const char* 	fe_current_id();

void		msg_show_body();

#endif
