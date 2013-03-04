
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <syslog.h>

#include "config.h"
#include "pfqhelp.h"
#include "pfqmessage.h"
#include "pfregex.h"
#include "pfqconfig.h"
#include "pfqlib.h"
#include "pfq_frontend.h"
#include "pfq_fencurses.h"

#define CAT_BUF_SIZE 20*1024
#define BUF_SIZE 250

char *a_names[]={
	"hold",
	"delete",
	"release",
	"requeue"
};

char *s_names[]={
	"unsorted",
	"sorted by from",
	"sorted by to",
	"sorted by subject"
};

int page_step;
int MSGMARK;

char* cat_buf;
char* regexps;

struct msg_list_t *msg_list;	/* Array of CURRENTLY SHOWN msg ids */
int msg_num;			/* Number of messages in the queue */

time_t last_repaint;

int half_delay_time;
int show_body_win;
int show_body_always;
int show_body_fromline;

void wnd_border ( WINDOW* wnd ) {
	wborder ( wnd, 0, 0, 0, 0, 0, 0, 0, 0 );
}

void wnd_clear ( WINDOW* wnd, const char* title ) {
	wclear ( wnd );
	wnd_border ( wnd );
	wattron ( wnd, A_BOLD );
	mvwaddstr ( wnd, 0,2, title );
	wattroff ( wnd, A_BOLD );
}

void wnd_prompt ( WINDOW* wnd, int y, int x, char* buf, int buflen ) {
	memset ( buf, 0, buflen );
	mvwaddstr ( wnd, y, x, buf );
	mvwgetnstr ( wnd, y, x, buf, buflen ); 
}

int wnd_confirm ( const char *action ) {
	int c, ret;

	WINDOW *w = newwin ( 5, 50, (LINES-5)/2, (COLS-50)/2 );
	wnd_border ( w );
	
	if ( pfql_getstatus(pfql_ctx)->use_colors )
		wbkgd ( w, COLOR_PAIR(CP_DIALOG) );

	nocbreak();
	cbreak();
	
	if ( pfql_getstatus(pfql_ctx)->wrk_tagged || (pfql_getstatus(pfql_ctx)->auto_wrk_tagged && pfql_num_tag(pfql_ctx)) )
		mvwprintw ( w, 2, 2, "Do you really want to %s tagged messages?", action );
	else
		mvwprintw ( w, 2, 2, "Do you really want to %s this message?", action );
	wrefresh ( w );
	
	c = wgetch ( w );
	ret =  ( c=='y' || c=='Y' ? 1 : 0 );

	delwin ( w );

	halfdelay ( half_delay_time*10 );
	return ret;
}

int wnd_change_sort_field () {
	int c, ret;

	WINDOW *w = newwin ( 8, 50, (LINES-8)/2, (COLS-50)/2 );
	wnd_border ( w );
	
	if ( pfql_getstatus(pfql_ctx)->use_colors )
		wbkgd ( w, COLOR_PAIR(CP_DIALOG) );

	nocbreak();
	cbreak();
	
	mvwprintw ( w, 1, 2, "Choose sort field:" );
	mvwprintw ( w, 3, 2, "0: unsorted" );
	mvwprintw ( w, 4, 2, "1: from    (4 descendent)" );
	mvwprintw ( w, 5, 2, "2: to      (5 descendent)" );
	mvwprintw ( w, 6, 2, "3: subject (6 descendent)" );

	wrefresh ( w );
	
	c = wgetch ( w );

	if ( c>='0' && c<='6' )
		ret = c-'0';
	else
		ret = -1;

	delwin ( w );

	halfdelay ( half_delay_time*10 );
	return ret;

}

void wnd_getregexp () {
	char *tmp;
	
	WINDOW *w = newwin ( 5, 60, (LINES-5)/2, (COLS-60)/2 );
	if ( pfql_getstatus(pfql_ctx)->use_colors )
		wbkgd ( w, COLOR_PAIR(CP_DIALOG) );
	wnd_border ( w );

	echo();
	nocbreak();
	cbreak();

	mvwaddstr ( w, 1, 2, "Insert POSIX regular expression to search for:" );
	wrefresh ( w );

	wnd_prompt ( w, 3, 2, regexps, REGEXP_LEN );

	delwin ( w );
	halfdelay ( half_delay_time*10 );
	noecho();

	pfql_ctx->search_mode = SM_ALL;
	if ( !strncmp( regexps, "f:", 2 ) )
		pfql_ctx->search_mode = SM_FROM;
	if ( !strncmp( regexps, "t:", 2 ) )
		pfql_ctx->search_mode = SM_TO;
	if ( !strncmp( regexps, "e:", 2 ) )
		pfql_ctx->search_mode = SM_TO | SM_FROM;
	if ( !strncmp( regexps, "s:", 2 ) )
		pfql_ctx->search_mode = SM_SUBJ;

	if ( pfql_ctx->search_mode != ( SM_ALL ) ) {
		tmp = (char*) malloc ( REGEXP_LEN );
		strncpy ( tmp, regexps+2, REGEXP_LEN );
		strncpy ( regexps, tmp, REGEXP_LEN );
		free ( tmp );
	}
		
	return;
}

void help() {
	WINDOW *w;
	char *ht;
	int c;
	char *help_be;

	help_be = (char*)(malloc ( 1024 ) );
	sprintf ( help_be,
			"  pfqueue lib version: %s\n"
			"  Backend API version: %d\n"
			"  Config source: %s%s%s%s\n\n"
			"  Backend ID: %s\n"
			"  Backend version: %s\n"
			"  Backend lib: %s\n\n"
			"  Frontend name: %s\n"
			"  Frontend version: %s\n"
			"  Frontend lib: %s",
			pfql_version(), pfql_backend_apiversion(pfql_ctx), 
			(pfql_getconf(pfql_ctx)->config_from==0 ? "none" : "" ),
			(pfql_getconf(pfql_ctx)->config_from==1 ? "/etc only" : "" ),
			(pfql_getconf(pfql_ctx)->config_from==2 ? "HOME only" : "" ),
			(pfql_getconf(pfql_ctx)->config_from==3 ? "/etc and HOME" : "" ),
			pfql_backend_id(pfql_ctx), 
			pfql_backend_version(pfql_ctx),
			pfql_backend_path(pfql_ctx),
			pfql_ctx->frontend.pfqfe_name(),
			pfql_ctx->frontend.pfqfe_version(),
			pfql_ctx->frontend.path
			);

	w = newwin ( 25, 60, (LINES-26)/2,(COLS-60)/2 );
	if ( pfql_getstatus(pfql_ctx)->use_colors )
		wbkgd ( w, COLOR_PAIR(CP_DIALOG) );

	nocbreak();
	cbreak();

	ht = help_text1;

	c = 0;
	while ( c!='q' && c!='Q' ) {
		wclear ( w );
		mvwprintw ( w, 1, 0, ht );
		wnd_border ( w );
		wattron ( w, A_BOLD );
		mvwaddstr ( w, 0, 28, "Help" );
		mvwaddstr ( w, 0, 2, "pfqueue ver. " );
		mvwaddstr ( w, 0, 15, VERSION );
		mvwaddstr ( w, 23, 2, "Press q to exit help, any key for next page" );
		wattroff ( w, A_BOLD );
		wrefresh ( w );
		c = wgetch ( w );
		if ( ht==help_text1 )
			ht = help_text2;
		else if ( ht == help_text2 )
			ht = help_be;
		else
			ht = help_text1;
	}
	delwin ( w );

	free ( help_be );
	halfdelay ( half_delay_time*10 );
}

int goto_msg(int i) {
	int j;

	if ( i<0 || i>=msg_num )
		return 0;

	PREVROW = CURROW;
	PREVMSG = CURMSG;
	CURMSG  = i;
	CURROW  = (i-FIRSTMSG)+FIRSTROW;

	show_body_fromline = 0;

	if ( CURROW<FIRSTROW ) {
		CURROW = FIRSTROW;
		FIRSTMSG = CURMSG;
		return 1;
	}
	if ( CURROW>MAXROW ) {
		j = CURROW-MAXROW;
		FIRSTMSG += j;
		CURROW = MAXROW;
		return 1;
	}
	return 0;
}

int goto_next() {
	return goto_msg( CURMSG+1 );
}

int goto_prev() {
	return goto_msg( CURMSG-1 );
}

int goto_first() {
	return goto_msg(0);
}

int goto_last() {
	if ( msg_num )
		return goto_msg( msg_num-1 );
	return 0;
}

int goto_nextpage() {
	if ( CURMSG+page_step < msg_num-1 )
		return goto_msg ( CURMSG+page_step );
	else
		return goto_msg ( msg_num-1 );
}

int goto_prevpage() {
	if ( CURMSG > page_step )
		return goto_msg ( CURMSG-page_step );
	else
		return goto_msg ( 0 );
}

void win_header() {

	wattron ( hwnd, A_BOLD );

	if ( pfql_getstatus(pfql_ctx)->use_colors )
		wattron ( hwnd, COLOR_PAIR(CP_HEAD) );

	mvwprintw ( hwnd, 0, 1, "Queue: '%s', %d message%s, %d tagged, %s",
			pfql_queue_name(pfql_ctx,pfql_getstatus(pfql_ctx)->cur_queue), msg_num, 
			( msg_num != 1 ? "s" : "" ),
			pfql_num_tag(pfql_ctx),
			s_names[pfql_getstatus(pfql_ctx)->sort_field]);
	wclrtoeol ( hwnd );

	wattroff ( hwnd, A_REVERSE );
	if ( pfql_getstatus(pfql_ctx)->auto_wrk_tagged )
		wattron ( hwnd, A_REVERSE );
	mvwaddstr ( hwnd, 0, COLS-7, "A" );
		
	wattroff ( hwnd, A_REVERSE );
	if ( pfql_getstatus(pfql_ctx)->wrk_tagged )
		wattron ( hwnd, A_REVERSE );
	mvwaddstr ( hwnd, 0, COLS-6, "T" );
		
	wattroff ( hwnd, A_REVERSE );
	if ( pfql_getstatus(pfql_ctx)->ask_confirm )
		wattron ( hwnd, A_REVERSE );
	mvwaddstr ( hwnd, 0, COLS-5, "C" );
		
	wattroff ( hwnd, A_REVERSE );
	if ( pfql_getstatus(pfql_ctx)->do_scan )
		wattron ( hwnd, A_REVERSE );
	mvwaddstr ( hwnd, 0, COLS-4, "S" );

	wattroff ( hwnd, A_REVERSE );
	if ( show_body_always )
		wattron ( hwnd, A_REVERSE );
	mvwaddstr ( hwnd, 0, COLS-3, "B" );

	wattroff ( hwnd, A_REVERSE );
	wattron ( hwnd, A_BOLD );
	mvwaddstr ( hwnd, 1, 1, "ID" );
	mvwaddstr ( hwnd, 1, COL1, "From" );
	mvwaddstr ( hwnd, 1, COL2, "To" );
	wattroff ( hwnd, A_BOLD );
	wattroff ( hwnd, COLOR_PAIR(CP_HEAD) );
	wnoutrefresh ( hwnd );
}

// Sizes the window according to the (new) screen size
void win_resize () {
	int mwnd_tot;
	int bwnd_tot;
	int lwnd_tot;
	
	bwnd_tot = bwnd_height + 2;
	if ( mwnd )
		delwin ( mwnd );
	if ( mwndb )
		delwin ( mwndb );
	if ( bwnd )
		delwin ( bwnd );
	if ( bwndb )
		delwin ( bwndb );
	if ( lwnd )
		delwin ( lwnd );
	if ( hwnd )
		delwin ( hwnd );
	if ( bline )
		free ( bline );
	if ( msg_list )
		free ( msg_list );

	last_repaint = 0;
	mainwnd = initscr();
	FIRSTROW = 0;

	if ( !has_colors() || start_color()==ERR )
		pfql_getstatus(pfql_ctx)->use_colors = 0;

	bline = (char*)malloc(sizeof(char)*COLS+1);
	memset ( bline, ' ', sizeof(char)*COLS+1 );
	*(bline+COLS) = 0;
	msg_list = (struct msg_list_t*)malloc(sizeof(struct msg_list_t)*(LINES));

	mwnd_tot = 7;
	lwnd_tot = LINES-mwnd_tot-2;
	if ( show_body_win )
		lwnd_tot -= bwnd_height;

	MAXROW = LINES-(mwnd_tot)-1-2;
	if ( show_body_win )
		MAXROW -= bwnd_height;
		
	hwnd  = newwin ( 2, COLS, 0, 0 );
	lwnd  = newwin ( lwnd_tot, COLS, 2, 0 );
	mwndb = newwin ( mwnd_tot, COLS, lwnd_tot+2, 0 );
	mwnd  = newwin ( mwnd_tot-2, COLS-2, lwnd_tot+3, 1 );
	if ( show_body_win ) {
		bwndb = newwin ( bwnd_height,   COLS,   lwnd_tot+2+mwnd_tot,   0 );
		bwnd  = newwin ( bwnd_height-2, COLS-2, lwnd_tot+2+mwnd_tot+1, 1 );
	}

	if ( pfql_getstatus(pfql_ctx)->use_colors ) {
		init_pair ( CP_MSG, COLOR_WHITE, COLOR_MAGENTA );
		init_pair ( CP_HEAD,COLOR_WHITE, COLOR_BLUE );
		init_pair ( CP_TAG, COLOR_YELLOW, COLOR_BLACK );
		init_pair ( CP_DIALOG, COLOR_WHITE, COLOR_RED );
		init_pair ( CP_OPTDIS, COLOR_RED, COLOR_BLUE );
		wbkgd ( mwndb, COLOR_PAIR(CP_HEAD) );
		if ( show_body_win )
			wbkgd ( bwndb, COLOR_PAIR(CP_HEAD) );
		wbkgd ( hwnd, COLOR_PAIR(CP_HEAD) );
	}

	if ( pfql_getstatus(pfql_ctx)->use_envelope )
		wnd_clear ( mwndb, "Message (from envelope)" );
	else
		wnd_clear ( mwndb, "Message (from headers)" );
	
	if ( show_body_win )
		wnd_clear ( bwndb, "Message body" );

	COL1 = 20;
	COL2 = COL1+( COLS - COL1 ) / 2-2;

	noecho();
	halfdelay(half_delay_time*10);

	wnoutrefresh ( mwndb );
	if ( show_body_win )
		wnoutrefresh ( bwndb );

	page_step = MAXROW ;

	doupdate();
}

// Displays the bottom window
void msg_show_headers () {
	struct msg_t *msg;
	
	if ( !msg_num ) {
		wclear ( mwnd );
		mvwaddstr ( mwnd, 0, 0, "Queue is empty" );
		return;
	}
	
	msg = pfql_msg( pfql_ctx, msg_list[CURROW].id );
	if ( !msg )
		return;
	
	pfql_retr_status ( pfql_ctx, msg->id );
	pfql_retr_headers( pfql_ctx, msg->id );

	mvwaddstr ( mwnd, 0, 0, "Message:" );
	wclrtobot ( mwnd );
	mvwaddstr ( mwnd, 0, 9, msg->id );

	mvwaddstr ( mwnd, 1, 0, "From..:" );
	mvwaddstr ( mwnd, 1, 8, msg->from );

	mvwaddstr ( mwnd, 2, 0, "To....:" );
	mvwaddstr ( mwnd, 2, 8, msg->to );

	mvwaddstr ( mwnd, 3, 0, "Subj..:" );
	mvwaddstr ( mwnd, 3, 8, msg->subj );

	mvwaddstr ( mwnd, 4, 0, "Status:" );
	mvwaddstr ( mwnd, 5, 0, " " );
	mvwaddstr ( mwnd, 4, 8, msg->stat );

	if ( show_body_always && show_body_win )
		msg_show_body(0);
}

// Returns the message displayed at row r
int msg_at(int r) {
	return (FIRSTMSG+r-FIRSTROW);
}

// Clears the cursor in the previous position and displays the current one
void show_cursor() {
	char *buf;
	int i;
	struct msg_t *msg;
	
	buf = (char*)malloc(BUF_SIZE);

	// Current row
	i = msg_at(CURROW);
	mvwinnstr ( lwnd, CURROW, 0, buf, COLS );

	if ( pfql_getstatus(pfql_ctx)->use_colors )
		wattron ( lwnd, COLOR_PAIR(CP_MSG) );
	else
		wattron ( lwnd, A_REVERSE );

	msg = pfql_msg_at(pfql_ctx,i);
	if ( msg && msg->tagged )
		wattron ( lwnd, A_BOLD );
	
	mvwaddstr ( lwnd, CURROW, 0, buf );
	wattroff ( lwnd, COLOR_PAIR(CP_MSG) );
	wattroff ( lwnd, A_BOLD );
	wattroff ( lwnd, A_REVERSE );

	// Previous row
	if ( PREVROW!=CURROW ) {
		i = msg_at(PREVROW);
		mvwinnstr ( lwnd, PREVROW, 0, buf, COLS );
		msg = pfql_msg_at(pfql_ctx,i);
		if ( msg ) {
			if ( msg->tagged )
				wattron ( lwnd, A_BOLD );
			if ( msg->tagged && pfql_getstatus(pfql_ctx)->use_colors )
				wattron ( lwnd, COLOR_PAIR(CP_TAG) );
		}
		mvwaddstr ( lwnd, PREVROW, 0, buf );
	}

	free ( buf );
	wattroff ( lwnd, COLOR_PAIR(CP_MSG) );
	wattroff ( lwnd, COLOR_PAIR(CP_TAG) );
	wattroff ( lwnd, A_BOLD );
	wattroff ( lwnd, A_REVERSE );

	wrefresh ( hwnd );

	wmove ( lwnd, CURROW, 0 );

	PREVROW = CURROW;
	
	msg_show_headers();
	wrefresh ( mwnd );
}

void msg_cat_body( WINDOW *pw, WINDOW* bw, int wl, int wc, int ctrls ) {
	char lines[MSG_MAX_ROWS][MSG_MAX_COLS];
	int j, i;
	int c;
	int fline;
	int pgstep;
	int msglines;
	int ptr;
	int buflen;
	int maxcol;

	maxcol = wc-2;
	memset ( lines, 0, sizeof(lines) );
	
	buflen = pfql_retr_body ( pfql_ctx, msg_list[CURROW].id, cat_buf, CAT_BUF_SIZE );
	if ( buflen == PFQL_MSGNOTEX )
		return;

	msglines = 0;
	ptr = 0;

	// Copy cat_buf into splitted lines
	while ( ptr<buflen && msglines < MSG_MAX_ROWS ) {
		c = 0;
		while ( c < maxcol && *(cat_buf+ptr)!='\n' ) {
			lines[msglines][c] = *(cat_buf+ptr);
			ptr++;
			c++;
		}
		while ( *(cat_buf+ptr)!='\n' )
			ptr++;
		ptr++;
		msglines++;
	}

	keypad ( pw, TRUE );
	pgstep = wl;
	
	if ( bw ) {
		wattron ( bw, A_BOLD );
		mvwaddstr ( bw, 0, 2, "Body of" );
		mvwaddstr ( bw, 0, 10, pfql_msg_at(pfql_ctx,CURMSG)->path );
		wattroff ( bw, A_BOLD );
		wrefresh ( bw );
	}

	c = 0;
	if ( ctrls )
		fline = 0;
	else
		fline = show_body_fromline;

	while ( c!='q' && c!='Q' ) {
		j = fline;
		i = 0;
		while ( j<MSG_MAX_ROWS && i < wl ) {
			mvwaddnstr ( pw, i++, 2, lines[j++], maxcol );
			wclrtoeol ( pw );
		}
		wrefresh ( pw );

no_fill:
		if ( ctrls )
			c = wgetch (pw);
		else
			c = 'q';

		switch (c) {
		case 'g':
		case KEY_HOME:
			fline = 0;
			break;
		case 'G':
		case KEY_END:
			fline = msglines-pgstep;
			break;
		case KEY_PPAGE:
			if ( fline > pgstep )
				fline -= pgstep;
			else
				fline = 0;
			break;
		case KEY_NPAGE:
			if ( fline < (msglines-pgstep) )
				fline += pgstep;
			break;
		case KEY_UP:
			if ( fline )
				fline--;
			break;
		case ' ':
		case KEY_DOWN:
			if ( fline<msglines-pgstep )
				fline++;
			break;
		case 'q':
		case 'Q':
			break;
		default:
			goto no_fill;
			break;
		}
	}

	halfdelay ( half_delay_time*10 );
}

void msg_show_body(int force_own) {
	WINDOW *w1, *w2;
	if ( show_body_win && !force_own )
		msg_cat_body ( bwnd, bwndb, bwnd_height-2, COLS-2, 0 );
	else {
		w1 = newwin ( LINES-4, COLS-4, 2, 2 );
		w2 = newwin ( LINES-6, COLS-6, 3, 3 );
		wnd_border ( w1 );
		wrefresh ( w1 );
		msg_cat_body ( w2, w1, LINES-6, COLS-6, 1 );
		delwin ( w2 );
		delwin ( w1 );
	}
}

// Returns 1 if queue needs repaint
int queue_needs_repaint () {
	return ( pfql_queue_last_changed(pfql_ctx) >= last_repaint );
}

// Set repaint flag
void queue_force_repaint(int f) {
	if ( f )
		last_repaint = 0;
	else
		last_repaint = pfql_queue_last_changed(pfql_ctx);
}

// Displays the list of messages
void queue_show() {
int i, j;
//time_t show_start;
struct msg_t *msg;

	j = FIRSTROW;
	i = FIRSTMSG;

	msg_num = pfql_num_msg(pfql_ctx);
	
	last_repaint = time(NULL);

	if ( pfql_getstatus(pfql_ctx)->sort_field!=PFQL_SORT_UNSORTED )
		while ( pfql_ctx->pfql_status.queue_status!=PFQL_Q_IDLE ) { usleep(5000); }

	while ( j <= MAXROW ) { 

		if ( i<msg_num ) {

			msg = pfql_msg_at(pfql_ctx,i);
			if ( !msg )
				return;
			strcpy ( msg_list[j].id, msg->id );

			pfql_retr_headers ( pfql_ctx,msg->id );

			if ( msg->tagged ) {
				wattron ( lwnd, A_BOLD );
				if ( pfql_getstatus(pfql_ctx)->use_colors )
					wattron ( lwnd, COLOR_PAIR(CP_TAG) );
			}
			mvwaddnstr  ( lwnd, j, 0, bline, COLS );
			mvwaddnstr ( lwnd, j, 1, msg->id, sizeof(msg->id) );
			mvwaddnstr ( lwnd, j, COL1,   msg->from, COL2-2-COL1 );
			mvwaddstr  ( lwnd, j, COL2-2, "  " );
			mvwaddnstr ( lwnd, j, COL2,   msg->to, COLS-2-COL2 );
			wattroff ( lwnd, A_BOLD );
			wattroff ( lwnd, COLOR_PAIR(CP_TAG) );
		} else {
			mvwaddstr ( lwnd, j, 1, " " );
			wclrtobot ( lwnd );
			j = MAXROW+1; // Force loop exit
		}
		j++;
		i++;
	}

	if ( msg_num && CURMSG > (msg_num-1) )
		goto_last();
	keypad ( lwnd, 1 );
	win_header();
	show_cursor();
	wnoutrefresh ( mwnd );
	wnoutrefresh ( lwnd );
	doupdate();
	wrefresh ( lwnd );
}

// Init frontend
int fe_init() {
	cat_buf = 0;
	bwnd_height = 10;

	cat_buf = (char*)malloc(CAT_BUF_SIZE);
	if ( !cat_buf )
		return 0;

	regexps = 0;
	regexps = (char*)malloc(REGEXP_LEN);
	if ( !regexps )
		return 0;

	mainwnd = mwndb = lwnd = mwnd = hwnd = bwnd = bwndb = NULL;
	bline = 0;
	msg_list = 0;
	return 1;
}

void fe_close() {
	if ( cat_buf )
		free ( cat_buf );
	if ( msg_list )
		free ( msg_list );
	if ( bline )
		free ( bline );
	if ( regexps )
		free ( regexps );
}

void queue_change(int q) {
int r;

	r = pfql_set_queue(pfql_ctx,q);
	if ( r==PFQL_OK ) {
		CURMSG = 0;
		goto_first();
		win_header();
	}
}

void msg_action(const char* id, int act) {
	if ( !msg_num )
		return;
	if ( (pfql_getstatus(pfql_ctx)->ask_confirm && wnd_confirm(a_names[act])) ||
	     (!pfql_getstatus(pfql_ctx)->ask_confirm) )
		pfql_msg_action(pfql_ctx, id, act);
	queue_force_repaint(1);
}

void main_loop() {
int c, r;
int i;

	c = 0;
	CURMSG = 0;
	MSGMARK = -1;

	FIRSTMSG = 0;

	while ( c != 'q' && c != 'Q' ) {

		if ( queue_needs_repaint() )
			queue_show();
		else
			show_cursor();

		c = wgetch ( lwnd );
		
		switch ( c ) {
		case 'G':
		case KEY_END:
			queue_force_repaint ( goto_last() );
			break;
		case 'g':
		case KEY_HOME:
			queue_force_repaint ( goto_first() );
			break;
		case KEY_UP:
			queue_force_repaint ( goto_prev() );
			break;
		case KEY_DOWN:
			queue_force_repaint ( goto_next() );
			break;
		case KEY_PPAGE:
			queue_force_repaint ( goto_prevpage() );
			break;
		case KEY_NPAGE:
			queue_force_repaint ( goto_nextpage() );
			break;
		case KEY_RESIZE:
			win_resize();
			break;
		case 'e':
			pfql_toggle_envelope(pfql_ctx);
			queue_force_repaint ( 1 );
			break;
		case 'd':
		case KEY_DC:
			msg_action( msg_list[CURROW].id, MSG_DELETE );
			break;
		case 'h':
			msg_action( msg_list[CURROW].id, MSG_HOLD );
			break;
		case 'l':
			msg_action ( msg_list[CURROW].id, MSG_RELEASE );
			break;
		case 'r':
			msg_action ( msg_list[CURROW].id, MSG_REQUEUE );
			break;
		case '1':
			queue_change ( Q_DEFERRED );
			break;
		case '4':
			queue_change ( Q_HOLD );
			break;
		case '3':
			queue_change ( Q_INCOMING );
			break;
		case '2':
			queue_change ( Q_ACTIVE );
			break;
		case '5':
			queue_change ( Q_CORRUPT );
			break;
		case 'a':
			pfql_tag_all(pfql_ctx);
			queue_force_repaint ( 1 );
			win_header();
			break;
		case 'm':
			MSGMARK = CURMSG;
			pfql_msg_tag( pfql_ctx, msg_list[CURROW].id);
			break;
		case 'b':
			show_body_win = !show_body_win;
			win_resize();
			if ( show_body_win )
				msg_show_body(0);
			break;
		case 'B':
			show_body_always = !show_body_always;
			win_header();
			break;
		case 32:
		case 't':
			if ( !msg_num )
				break;
			if ( MSGMARK==-1 ) {
				pfql_msg_toggletag( pfql_ctx,msg_list[CURROW].id );
			} else {
				if ( CURMSG>=MSGMARK ) {
					for ( i=MSGMARK; i<=CURMSG; i++ )
						pfql_msg_tag(pfql_ctx,pfql_msg_at(pfql_ctx,i)->id);
				} else {
					for ( i=CURMSG; i<=MSGMARK; i++ ) 
						pfql_msg_tag(pfql_ctx,pfql_msg_at(pfql_ctx,i)->id);
				}
			}
			MSGMARK = -1;
			goto_next();
			queue_force_repaint ( 1 );
			show_cursor();
			win_header();
			break;
		case 'T':
			wnd_getregexp();
			pfql_msg_searchandtag(pfql_ctx,regexps);
			queue_force_repaint ( 1 );
			win_header();
			break;
		case 'u':
			pfql_tag_none(pfql_ctx);
			win_header();
			queue_force_repaint ( 1 );
			break;
		case ':':
			pfql_getstatus(pfql_ctx)->auto_wrk_tagged = !pfql_getstatus(pfql_ctx)->auto_wrk_tagged;
			win_header();
			break;
		case ';':
			pfql_getstatus(pfql_ctx)->wrk_tagged = !pfql_getstatus(pfql_ctx)->wrk_tagged;
			win_header();
			break;
		case '/':
			wnd_getregexp();
			i = pfql_msg_search(pfql_ctx,regexps);
			goto_msg ( i );
			queue_force_repaint ( 1 );
			break;
		case 'n':
			i = pfql_msg_searchnext(pfql_ctx,regexps);
			goto_msg ( i );
			break;
		case 'p':
			pfql_msg_searchprev(pfql_ctx,regexps);
			break;
		case '?':
			help();
			queue_force_repaint ( 1 );
			break;
		case 'c':
			pfql_getstatus(pfql_ctx)->ask_confirm = !pfql_getstatus(pfql_ctx)->ask_confirm;
			win_header();
			break;
		case '-':
			pfql_getstatus(pfql_ctx)->do_scan = !pfql_getstatus(pfql_ctx)->do_scan;
			win_header();
			break;
		case '+':
			pfql_getstatus(pfql_ctx)->use_colors = !pfql_getstatus(pfql_ctx)->use_colors;
			queue_force_repaint ( 1 );
			win_resize();
			win_header();
			break;
		case '>':
			if ( bwnd_height<LINES-10 )
				bwnd_height++;
			win_resize();
			win_header();
			break;
		case '<':
			if ( bwnd_height>5 )
				bwnd_height--;
			win_resize();
			win_header();
			break;
		case ',':
			if ( show_body_fromline && show_body_win )
				show_body_fromline--;
			msg_show_body(0);
			break;
		case '.':
			if ( show_body_win ) {
				show_body_fromline++;
				msg_show_body(0);
			}
			break;
		case 13:
		case 10:
			msg_show_body(0);
			if ( !show_body_win ) {
				win_resize();
				queue_force_repaint ( 1 );
				msg_show_headers();
			}
			break;
		case 's':
			if ( msg_num ) {
				msg_show_body(1);
				win_resize();
				queue_force_repaint ( 1 );
				msg_show_headers();
			}
			break;
		case 'S':
			r = wnd_change_sort_field();
			if ( r != -1 ) {
				if ( r<=3 ) {
					pfql_getstatus(pfql_ctx)->sort_field = r;
					pfql_getstatus(pfql_ctx)->sort_sense = PFQL_SORT_ASC;
				} else {
					pfql_getstatus(pfql_ctx)->sort_field = r-3;
					pfql_getstatus(pfql_ctx)->sort_sense = PFQL_SORT_DESC;
				}
			}
			queue_force_repaint ( 1 );
		}
		
	}
}

int pff_init(void* ctx) {
	pfql_ctx = ctx;
	return PFQL_OK;
}

int pff_start() {
	// Some defaults
	pfql_ctx->regexp = 0;
	half_delay_time = 1;
	show_body_win = 0;
	show_body_always = 1;

	win_resize();
	queue_show();
	main_loop();

	fe_close();
	endwin();

	return 0;
}

const char* pff_name() {
	return "ncurses";
}

const char* pff_version() {
	return "1.0";
}

void pff_set_path(const char* p) {
	char *c = pfql_ctx->frontend.path;
	strncpy ( c, p, sizeof(pfql_ctx->frontend.path));
}
