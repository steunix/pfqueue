
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

#include "pfq_backend.h"
#include "pfq_service.h"
#include "../pfqmessage.h"
#include "../config.h"
#include "../pfqtcp.h"

#define PF_VERSION_20	1
#define PF_VERSION_21	2
#define PF_VERSION_22	3

pthread_mutex_t		socket_mutex = PTHREAD_MUTEX_INITIALIZER;

int CURQ;

int NUMMSG_THREAD;

struct sockaddr_in svra;
struct hostent *svr;
int    sock;

struct pfb_conf_t pfb_conf;

void strip_nl(char* b, int l) {
	int i;
	for ( i=0; i<l; i++ ) {
		if (*(b+i)=='\n')
			*(b+i) = 0;
	}
}

int w_socket ( int s, const char* b ) {
	write ( s, b, strlen(b) );
	return 0;
}

int r_socket ( int s, char *b, size_t l ) {
	int rd;
	memset ( b, 0, l );
	rd = read ( s, b, l );
	if ( rd>0 )
		strip_nl(b, l);
	else
		return -2;
	if ( !strncmp( b, "ERR", 3 ) )
		return -1;
	return 0;
}

int wr_socket ( int s, char *b, size_t l ) {
	int res;
	pthread_mutex_lock(&socket_mutex);
	w_socket ( s, b );
	res = r_socket ( s, b, l );
	pthread_mutex_unlock(&socket_mutex);
	return res;
}

const char* pfb_id() {
	return "socket";
}

int pfb_apiversion() {
	return PFQ_API_VERSION;
}

const char* pfb_version() {
	return "1.0";
}

const char* pfb_path() {
	return pfb_conf.backend_path;
}

struct pfb_conf_t *pfb_getconf() {
	return &pfb_conf;
}

struct msg_t* msg_from_id(const char* mid) {
int i;
	for ( i=0; i<NUMMSG_THREAD; i++ ) {
		if ( !strncmp(ext_queue[i].id, mid, sizeof(ext_queue[i].id) ) )
			return &ext_queue[i];
	}
	return NULL;
}

int pfb_init() {
	pfb_conf.max_char = 200;
	strcpy ( pfb_conf.command_path, "" );
	strcpy ( pfb_conf.config_path, "" );
	pfb_conf.port = 20000;
	return PFBE_OK;
}

void pfb_set_path(const char* p) {
	strncpy ( pfb_conf.backend_path, p, 200 );
}

int pfb_setup( struct msg_t *qptr1, struct be_msg_t *qptr2 ) {

	sock = socket(AF_INET, SOCK_STREAM, 0 );
	if ( sock<0 )
		return PFBE_UNUSABLE;
	svr = gethostbyname ( pfb_conf.host );
	if ( svr == NULL )
		return PFBE_UNUSABLE;
	memset ( &svra, 0, sizeof(svra) );
	svra.sin_family = AF_INET;

	memcpy ( (struct sockaddr*)&svra.sin_addr.s_addr, 
		(struct hostent*)svr->h_addr, 
		(struct hostent*)svr->h_length );
	svra.sin_port = htons( pfb_conf.port );

	if ( connect(sock, (struct sockaddr*)&svra, sizeof(svra)) <0 )
		return PFBE_UNUSABLE;

	ext_queue = qptr1;
	my_queue  = qptr2;

	pthread_mutex_unlock(&socket_mutex);
	return PFBE_OK;
}

int pfb_close() {
	w_socket ( sock, "QUIT\n" );
	shutdown ( sock, SHUT_RDWR );
	return PFBE_OK;
}

int pfb_retr_headers( const char* msgid ) {
	int res;
	struct msg_t *msg;

	msg = msg_from_id(msgid);
	if ( msg && msg->hcached )
		return PFBE_OK;

	res = pfb_retr_to(msgid);
	res|= pfb_retr_from(msgid);
	res|= pfb_retr_subj(msgid);
	res|= pfb_retr_path(msgid);
	
	res = 0;
	if ( res == PFBE_OK )
		msg->hcached = 1;
	else
		msg->hcached = 0;

	return PFBE_OK;
}

int pfb_retr_id( int n, char* b, size_t len ) {
	char buf[BUF_SIZE];
	int res;

	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s %d\n", CMD_MSGID, n );
	res = wr_socket ( sock, buf, sizeof(buf) );

	if ( res )
		strncpy ( b, PFBE_SERROR, len );
	else
		strncpy ( b, buf+CMD_REPLY_LEN, len );

	return PFBE_OK;
}

int pfb_retr_path( const char* msgid ) {
	char buf[BUF_SIZE];
	int res;
	struct msg_t *msg;

	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_ERROR;

	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s %s\n", CMD_PATH, msgid );
	res = wr_socket ( sock, buf, sizeof(buf) );
	
	if ( res )
		strcpy ( msg->path, PFBE_SERROR );
	else
		strcpy ( msg->path, buf+CMD_REPLY_LEN );

	return PFBE_OK;
}

int pfb_retr_from( const char* msgid ) {
	char buf[BUF_SIZE];
	int res;
	struct msg_t *msg;

	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_ERROR;

	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s %s\n", CMD_FROM, msgid );
	res = wr_socket ( sock, buf, sizeof(buf) );
	
	if ( res )
		strcpy ( msg->from, PFBE_SERROR );
	else
		strcpy ( msg->from, buf+CMD_REPLY_LEN );

	return PFBE_OK;
}

int pfb_retr_to( const char* msgid ) {
	char buf[BUF_SIZE];
	int res;
	struct msg_t *msg;

	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_ERROR;

	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s %s\n", CMD_TO, msgid );
	res = wr_socket ( sock, buf, sizeof(buf) );
	
	if ( res )
		strcpy ( msg->to, PFBE_SERROR );
	else
		strcpy ( msg->to, buf+CMD_REPLY_LEN );

	return PFBE_OK;

}

int pfb_retr_subj( const char* msgid ) {
	char buf[BUF_SIZE];
	int res;
	struct msg_t *msg;

	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_ERROR;

	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s %s\n", CMD_SUBJ, msgid );
	res = wr_socket ( sock, buf, sizeof(buf) );
	
	if ( res )
		strcpy ( msg->subj, PFBE_SERROR );
	else
		strcpy ( msg->subj, buf+CMD_REPLY_LEN );

	return PFBE_OK;
}

int pfb_retr_status ( const char* msgid ) {
	char buf[BUF_SIZE];
	int res;
	struct msg_t *msg;

	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_ERROR;

	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s %s\n", CMD_STATUS, msgid );
	res = wr_socket ( sock, buf, sizeof(buf) );
	
	if ( res )
		strcpy ( msg->stat, PFBE_SERROR );
	else
		strcpy ( msg->stat, buf+CMD_REPLY_LEN );

	return PFBE_OK;
}

int pfb_retr_body( const char* msgid, char *buffer, size_t buflen ) {
	int res;
	struct msg_t *msg;
	char *buf2;

	buf2 = (char*)malloc(buflen);

	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_ERROR;
	
	memset ( buf2, 0, buflen );
	sprintf ( buf2, "%s %s\n", CMD_BODY, msgid );
	res = wr_socket ( sock, buf2, buflen );

	sprintf ( buffer, "%s\n", buf2+CMD_REPLY_LEN+7 );

	free ( buf2 );

	return strlen(buf2);
}

int pfb_num_msg() {
	char buf[BUF_SIZE];
	int res;
	
	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s\n", CMD_NUMMSG );
	res = wr_socket ( sock, buf, sizeof(buf) );

	if ( res )
		return 0;
	else
		return ( atoi(buf+CMD_REPLY_LEN) );
}

int pfb_queue_count() {
	char buf[BUF_SIZE];
	int res;

	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s\n", CMD_NUMQ );
	res = wr_socket ( sock, buf, sizeof(buf) );
	printf ( "res: %d\n", res );
	
	if ( res )
		return 0;
	else
		return atoi(buf+CMD_REPLY_LEN);
}

char* pfb_queue_name(int q) {
	static char buf[BUF_SIZE];
	int res;

	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s %d\n", CMD_QNAME, q );
	res = wr_socket ( sock, buf, sizeof(buf) );
	
	if ( res )
		return 0;
	else
		return buf+CMD_REPLY_LEN;
}

int pfb_fill_queue() {
	char buf[255];
	int i, j;
	struct be_msg_t *msg;

	j = pfb_num_msg();
	for ( i=0; i<j; i++ ) {
		msg = &my_queue[i];
		pfb_retr_id ( i, buf, sizeof(buf) );
		memcpy ( msg->id, buf, sizeof(msg->id) );
		msg->changed = strncmp( msg->id, ext_queue[i].id, strlen(msg->id) );
	}
	NUMMSG_THREAD = j;
	return j;
}

int pfb_action(int act, const char* msg) {
	char b[BUF_SIZE];
	char b2[BUF_SIZE];

	switch ( act ) {
	case MSG_DELETE:
		sprintf ( b, CMD_MSGDEL );
		break;
	case MSG_HOLD:
		sprintf ( b, CMD_MSGHOLD );
		break;
	case MSG_RELEASE:
		sprintf ( b, CMD_MSGREL );
		break;
	case MSG_REQUEUE:
		sprintf ( b, CMD_MSGREQ );
		break;
	default:
		return 1;
	}
	sprintf ( b2, "%s %s\n", b, msg );
	wr_socket ( sock, b2, sizeof(b2) );

	return PFBE_OK;
}

int pfb_message_delete( const char* msg ) {
	return pfb_action ( MSG_DELETE, msg );
}

int pfb_message_hold( const char* msg ) {
	return pfb_action ( MSG_HOLD, msg );
}

int pfb_message_requeue( const char* msg ) {
	return pfb_action ( MSG_REQUEUE, msg );
}

int pfb_message_release( const char* msg ) {
	return pfb_action ( MSG_RELEASE, msg );
}

int pfb_set_queue ( int q ) {
	char buf[BUF_SIZE];
	int res;
	
	memset ( buf, 0, sizeof(buf) );
	sprintf ( buf, "%s %d\n", CMD_SETQ, q );
	res = wr_socket ( sock, buf, sizeof(buf) );

	return PFBE_OK;
}

void pfb_use_envelope ( int u ) {
	pfb_using_envelope = u;
}

int pfb_get_caps() {
	return pfb_caps;
}


