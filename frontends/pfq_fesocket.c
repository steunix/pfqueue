
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

#include "pfqtcp.h"
#include "config.h"
#include "pfqhelp.h"
#include "pfqmessage.h"
#include "pfregex.h"
#include "pfqconfig.h"
#include "pfqlib.h"
#include "pfq_frontend.h"

#define CAT_BUF_SIZE 20*1024
#define BUF_SIZE 250

int svrs;	// Server socket
struct sockaddr_in svra;

char *a_names[]={
	"hold",
	"delete",
	"release",
	"requeue"
};

int pfqMSGMARK;

char* cat_buf;
char* regexps;

struct pfql_context_t *pfql_ctx;

struct msg_list_t *msg_list;	/* Array of CURRENTLY SHOWN msg ids */
int msg_num;			/* Number of messages in the queue */

void version() {
	printf ( "spfqueue - Version %s - (C) 2004-2007 Stefano Rivoir\n",
			VERSION );
}

int w_socket ( int s, const char* b ) {
	write ( s, b, strlen(b) );
	printf ( b );
}

void strip_nl(char* b, int l) {
	int i;
	for ( i=0; i<l; i++ ) {
		if (*(b+i)=='\n')
			*(b+i) = 0;
	}
}

void usage() {
	version();
	printf ( "Usage: spfqueue [-ehvn] [-b postfix1|postfix2|exim] [-q queue_num] [-m max_msg]\n"
		 "       [-s autorefresh_seconds] [-l limit_seconds] -[B backends_path]\n"
		 "       [-p executables_path] [-c config_path]\n" );
}

void fe_close() {
	if ( cat_buf )
		free ( cat_buf );
	if ( regexps )
		free ( regexps );
}

void msg_action( int cs, const char* id, int act) {
	char buf[BUF_SIZE];
	pfql_msg_action(pfql_ctx, id, act);
	sprintf ( buf, "%s: OK\n", CMD_REPLY );
	w_socket ( cs, buf );
}

void client_send_body ( int cs, const char *id ) {
	int buflen;
	char cat_buf2[CAT_BUF_SIZE];
	struct msg_t *msg;

	if ( !msg ) {
		sprintf ( cat_buf2, "%s: NOMSG\n", CMD_ERROR );
	} else {
		buflen = pfql_retr_body ( pfql_ctx, id, cat_buf, CAT_BUF_SIZE );

		if ( buflen )
			sprintf ( cat_buf2, "%s: %.6d,%s\n", CMD_REPLY, buflen, cat_buf );
		else
			sprintf ( cat_buf2, "%s: ERR\n", CMD_ERROR );
	}
	w_socket ( cs, cat_buf2 );
}

void client_send_from ( int cs, const char *id ) {
	char buf[BUF_SIZE];
	struct msg_t *msg;

	msg = pfql_msg(pfql_ctx,id);

	if ( !msg ) {
		sprintf ( buf, "%s: NOMSG\n", CMD_ERROR );
		w_socket ( cs, buf );
		return;
	}

	pfql_retr_headers(pfql_ctx,id);
	sprintf ( buf, "%s: %s\n", CMD_REPLY, msg->from );
	w_socket ( cs, buf );
}

void client_send_status ( int cs, const char *id ) {
	char buf[BUF_SIZE];
	struct msg_t *msg;

	msg = pfql_msg(pfql_ctx,id);

	if ( !msg ) {
		sprintf ( buf, "%s: NOMSG\n", CMD_ERROR );
		w_socket ( cs, buf );
		return;
	}

	pfql_retr_status(pfql_ctx,id);
	sprintf ( buf, "%s: %s\n", CMD_REPLY, msg->stat );
	w_socket ( cs, buf );
}

void client_send_path ( int cs, const char *id ) {
	char buf[BUF_SIZE];
	struct msg_t *msg;

	msg = pfql_msg(pfql_ctx,id);

	if ( !msg ) {
		sprintf ( buf, "%s: NOMSG\n", CMD_ERROR );
		w_socket ( cs, buf );
		return;
	}

	pfql_retr_headers(pfql_ctx,id);
	sprintf ( buf, "%s: %s\n", CMD_REPLY, msg->path );
	w_socket ( cs, buf );
}

void client_send_to ( int cs, char *id ) {
	char buf[BUF_SIZE];
	struct msg_t *msg;

	msg = pfql_msg(pfql_ctx,id);

	if ( !msg ) {
		sprintf ( buf, "%s: NOMSG\n", CMD_ERROR );
		w_socket ( cs, buf );
		return;
	}

	pfql_retr_headers(pfql_ctx,id);
	sprintf ( buf, "%s: %s\n", CMD_REPLY, msg->to );
	w_socket ( cs, buf );
}

void client_send_subj ( int cs, char *id ) {
	char buf[BUF_SIZE];
	struct msg_t *msg;

	msg = pfql_msg(pfql_ctx,id);

	if ( !msg ) {
		sprintf ( buf, "%s: NOMSG\n", CMD_ERROR );
		w_socket ( cs, buf );
		return;
	}

	pfql_retr_headers(pfql_ctx,id);
	sprintf ( buf, "%s: %s\n", CMD_REPLY, msg->subj );
	w_socket ( cs, buf );
}

void client_send_nummsg ( int cs ) {
	char buf[BUF_SIZE];
	sprintf ( buf, "%s: %.05d\n", CMD_REPLY, pfql_num_msg(pfql_ctx) );
	w_socket(cs, buf);
}

void client_send_numq ( int cs ) {
	char buf[BUF_SIZE];
	sprintf ( buf, "%s: %.05d\n", CMD_REPLY, pfql_num_queues(pfql_ctx) );
	w_socket(cs, buf);
}

void client_send_queue_name ( int cs, int q ) {
	char buf[BUF_SIZE];
	sprintf ( buf, "%s: %s\n", CMD_REPLY, pfql_queue_name(pfql_ctx,q) );
	w_socket(cs, buf);
}

void client_send_id ( int cs, int n ) {
	struct msg_t *msg;
	char buf[BUF_SIZE];
	msg = pfql_msg_at(pfql_ctx,n);
	
	if ( (!msg) || !strlen(msg->id) )
		sprintf ( buf, "%s: NOMSG\n", CMD_ERROR );
	else
		sprintf ( buf, "%s: %s\n", CMD_REPLY, msg->id );
	w_socket ( cs, buf );
}

void client_send_lastchanged ( int cs ) {
	char buf[BUF_SIZE];
	sprintf ( buf, "%s: %d\n", CMD_REPLY, pfql_queue_last_changed(pfql_ctx) );
	w_socket ( cs, buf );

}

void client_set_queue ( int cs, int q ) {
	char buf[BUF_SIZE];
	pfql_set_queue(pfql_ctx,q);
	sprintf ( buf, "%s: OK\n", CMD_REPLY );
	w_socket ( cs, buf );
}
	
void client_send_list ( int cs ) {
	char buf[2048];
	int i, n;
	struct msg_t *msg;
	
	if ( pfql_num_msg(pfql_ctx)<0 ) {
		sprintf ( buf, "%s: EMPTY\n", CMD_REPLY );
		w_socket ( cs, buf );
		return;
	}
	
	n = pfql_num_msg(pfql_ctx);
	write ( cs, CMD_REPLY, strlen(CMD_REPLY) );
	for ( i=0; i<n; i++ ) {
		msg = pfql_msg_at(pfql_ctx,i);
		write ( cs, msg->id, strlen(msg->id) );
		if ( i<n-1 )
			write ( cs, ",", 1 );
	}
	write ( cs, "\n", 1 );
}

int iscmd ( const char* b, const char* c) {
	return (!strncmp(b,c,strlen(c)));
}

void client_process ( int cs ) {
	char buf[BUF_SIZE];
	int ex, done, bl;
	int i;
	
	ex = 0;

	printf ( "Client process: %d\n", cs );
	while ( !ex ) {
		memset ( buf, 0, sizeof(buf) );
		bl = read ( cs, buf, sizeof(buf) );

		if ( bl ) {
			strip_nl ( buf, sizeof(buf) );
			printf ( buf );
			printf ( "\n" );
			done = 0;
		}

		if (iscmd( buf, CMD_NUMMSG)) {
			client_send_nummsg ( cs );
			done=1;
		}

		if (iscmd( buf, CMD_NUMQ )) {
			client_send_numq ( cs );
			done=1;
		}

		if (iscmd( buf, CMD_QNAME )) {
			i = atoi(buf+strlen(CMD_QNAME)+1 );
			client_send_queue_name ( cs, i );
			done=1;
		}

		if (iscmd( buf, CMD_SETQ )) {
			i = atoi(buf+strlen(CMD_SETQ)+1 );
			client_set_queue ( cs, i );
			done=1;
		}

		if (iscmd( buf, CMD_MSGID )) {
			i = atoi(buf+strlen(CMD_MSGID)+1 );
			client_send_id ( cs, i );
			done=1;
		}

		if (iscmd( buf, CMD_LSTIDS )) {
			client_send_list ( cs );
			done=1;
		}

		if (iscmd( buf, CMD_STATUS )) {
			client_send_status( cs, buf+strlen(CMD_STATUS)+1 );
			done=1;
		}

		if (iscmd( buf, CMD_PATH )) {
			client_send_path( cs, buf+strlen(CMD_PATH)+1 );
			done=1;
		}

		if (iscmd( buf, CMD_FROM )) {
			client_send_from( cs, buf+strlen(CMD_FROM)+1 );
			done=1;
		}

		if (iscmd( buf, CMD_TO )) {
			client_send_to( cs, buf+strlen(CMD_TO)+1 );
			done=1;
		}

		if (iscmd( buf, CMD_SUBJ )) {
			client_send_subj( cs, buf+strlen(CMD_SUBJ)+1 );
			done=1;
		}

		if (iscmd( buf, CMD_BODY )) {
			client_send_body( cs, buf+strlen(CMD_BODY)+1 );
		}

		if (iscmd( buf, CMD_QUIT )) {
			ex = 1;
			done=1;
		}

		if (iscmd( buf, CMD_MSGDEL )) {
			msg_action ( cs, buf+strlen(CMD_MSGDEL)+1, MSG_DELETE );
			done=1;
		}

		if (iscmd( buf, CMD_MSGREL )) {
			msg_action ( cs, buf+strlen(CMD_MSGREL)+1, MSG_RELEASE );
			done=1;
		}

		if (iscmd( buf, CMD_MSGREQ )) {
			msg_action ( cs, buf+strlen(CMD_MSGREQ)+1, MSG_REQUEUE );
			done=1;
		}

		if (iscmd( buf, CMD_MSGHOLD )) {
			msg_action ( cs, buf+strlen(CMD_MSGHOLD)+1, MSG_HOLD );
			done=1;
		}

		if ( !done )
			write ( cs, "ERR: NOCMD\n", 11 );
	}
	
}

void main_loop() {
int c;
int i;
int clis, clil;
struct sockaddr_in clia;

	c = 0;
	pfqMSGMARK = -1;

	clil = sizeof(clia);
	while ( 1 ) {
		clis = accept ( svrs, (struct sockaddr*)&clia, &clil );
			client_process ( clis );
			shutdown ( clis, SHUT_RDWR );
	}
}

void sig_clean_exit(int i) {
	//pfql_context_destroy(pfql_ctx);
	fe_close();
	printf ( "OK, quitting\n" );
	exit(0);
}

int fe_init() {
	cat_buf = 0;
	cat_buf = (char*)malloc(CAT_BUF_SIZE);
	if ( !cat_buf )
		return 0;
}

int pff_init(void *ctx) {
	pfql_ctx = ctx;
	return PFQL_OK;
}

int pff_start() {
	int clil;
	int st;

	/* Ignore pipes error */
	signal ( SIGPIPE, SIG_IGN );

	/* Ignore child errors */
	signal ( SIGCHLD, SIG_IGN );

	/* Trap SIGINT */
	signal ( SIGINT, sig_clean_exit );

	if ( !fe_init() ) {
		fe_close();
		fprintf ( stderr, "spfqueue: cannot fe_init!\n" );
		exit (-2);
	}

	// Initialize socket
	clil = sizeof(struct sockaddr_in);
	svrs = socket ( AF_INET, SOCK_STREAM, 0 );
	if ( svrs<0 ) {
		fprintf ( stderr, "spfqueue: cannot create socket\n" );
		return -1;
	}
	svra.sin_family = AF_INET;
	svra.sin_addr.s_addr = INADDR_ANY;
	svra.sin_port = htons(20000);
	if ( bind(svrs, (struct sockaddr*)&svra, sizeof(svra)) < 0 ) {
		fprintf ( stderr, "spfqueue: cannot bind socket\n" );
		return -1;
	}
	listen ( svrs, 5 );

	main_loop();

	fe_close();

	return 0;
}

const char* pff_name() {
	return "socket";
}

const char* pff_version() {
	return "1.0";
}

void pff_set_path(const char* p) {
	strncpy ( pfql_ctx->frontend.path, p, sizeof(pfql_ctx->frontend.path) );
}
