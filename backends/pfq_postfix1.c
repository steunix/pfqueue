
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

#include "pfq_backend.h"
#include "pfq_service.h"
#include "../pfqmessage.h"
#include "../config.h"

int CURQ;

int NUMMSG_THREAD;

char queue_path[BUF_SIZE];
char config_path[BUF_SIZE];
char pftools_path[BUF_SIZE];
char postconf_path[BUF_SIZE];
char postsuper_path[BUF_SIZE];
char postcat_path[BUF_SIZE];
int has_configpath;

#define HEADER_FROM	"From: "
#define HEADER_TO	"To: "
#define HEADER_SUBJECT  "Subject: "
#define HEADER_FROM_LEN 6
#define HEADER_TO_LEN   4
#define HEADER_SUBJECT_LEN 9
#define ENVELOPE_FROM	"sender: "
#define ENVELOPE_TO	"recipient: "
#define ENVELOPE_FROM_LEN 8
#define ENVELOPE_TO_LEN 11
#define ENVELOPE_NULL_SENDER "Null envelope sender"

char *q_names[]={
	"deferred",
	"hold",
	"incoming",
	"active"
};

struct pfb_conf_t pfb_conf;

struct pfb_conf_t *pfb_getconf() {
	return &pfb_conf;
}

int pfb_apiversion() {
	return PFQ_API_VERSION;
}

const char* pfb_id() {
	return "postfix 1.x";
}

const char* pfb_version() {
	return "1.2";
}

const char* pfb_path() {
	return pfb_conf.backend_path;
}

struct msg_t* msg_from_id(const char* mid) {
int i;
	for ( i=0; i<NUMMSG_THREAD; i++ ) {
		if ( !strncmp(ext_queue[i].id, mid, sizeof(ext_queue[i].id) ) )
			return &ext_queue[i];
	}
	return NULL;
}

int dir_dig( const char* basedir ) {
	DIR *bdir;
	struct dirent *dir;
	char full_path[BUF_SIZE];
	struct be_msg_t *msg;
	
	if ( NUMMSG_THREAD >= msg_max )
		return -1;

	if ( dig_limit && ( time(NULL)-dig_start > dig_limit ) )
		return -1;
	
	bdir = opendir ( basedir );
	while ( bdir && NUMMSG_THREAD < msg_max && ( dir = readdir ( bdir ) ) ) {

		if ( dig_limit && ( time(NULL)-dig_start > dig_limit ) )
			return -1;
		snprintf ( full_path, sizeof(full_path), "%s/%s", basedir, dir->d_name );

		if ( fs_should_dig ( dir, full_path ) ) {
			dir_dig ( full_path );
		} else if ( NUMMSG_THREAD < msg_max && fs_should_add ( dir, full_path ) ) {
			msg = &(my_queue[NUMMSG_THREAD]);
			memcpy ( msg->id, dir->d_name, sizeof(msg->id) );
			snprintf ( msg->path, sizeof(msg->path), "%s/%s",
				basedir, dir->d_name );
			msg->changed = strcmp( dir->d_name, ext_queue[NUMMSG_THREAD].id );
			NUMMSG_THREAD++;
		}
		
	}
	if ( bdir )
		closedir ( bdir );

	return PFBE_OK;
}

int pfb_init() {
	pfb_conf.max_char = 200;
	strcpy ( pfb_conf.command_path, "" );
	strcpy ( pfb_conf.config_path,  "" );
	return PFBE_OK;
}

void pfb_set_path(const char* p) {
	strncpy ( pfb_conf.backend_path, p, 200 );
}

int pfb_setup( struct msg_t *qptr1, struct be_msg_t *qptr2 ) {
	char pconf[BUF_SIZE];
	FILE *p;

	msg_max = pfb_conf.msg_max;
	dig_limit = pfb_conf.scan_limit;
	ext_queue = qptr1;
	my_queue = qptr2;

	CURQ = 0;
	pfb_using_envelope = 0;
	pfb_caps = BECAPS_MSG_HOLD | BECAPS_MSG_DEL | BECAPS_MSG_REQUEUE | BECAPS_MSG_ENVELOPE;

	memset ( config_path, 0, sizeof(config_path) );
	memset ( pftools_path, 0, sizeof(pftools_path) );
	memset ( postconf_path, 0, sizeof(postconf_path) );
	memset ( postsuper_path, 0, sizeof(postsuper_path) );
	memset ( postcat_path, 0, sizeof(postcat_path) );

	if ( strlen(pfb_conf.command_path) )
		snprintf ( pftools_path, BUF_SIZE-1, "%s", pfb_conf.command_path );

	if ( strlen(pfb_conf.config_path) ) {
		snprintf ( config_path, BUF_SIZE-1, "%s", pfb_conf.config_path );
		has_configpath = 1;
	}

	// If -p is not specified, use commands without path
	if ( strlen ( pftools_path ) ) {
		snprintf ( postconf_path, BUF_SIZE, "%s/postconf", pftools_path );
		snprintf ( postsuper_path, BUF_SIZE, "%s/postsuper", pftools_path );
		snprintf ( postcat_path, BUF_SIZE, "%s/postcat", pftools_path );
	} else {
		snprintf ( postconf_path, BUF_SIZE, "postconf" );
		snprintf ( postsuper_path, BUF_SIZE, "postsuper" );
		snprintf ( postcat_path, BUF_SIZE, "postcat" );
	}

	// Look for queue_directory
	if ( has_configpath ) 
		snprintf ( pconf, BUF_SIZE, "%s -c %s -h queue_directory 2> /dev/null", postconf_path, config_path );
	else
		snprintf ( pconf, BUF_SIZE, "%s -h queue_directory 2> /dev/null", postconf_path );
	p = popen ( pconf, "r" );
	if ( !p ) {
		syslog ( LOGLEVEL, "pfqueue postfix1 backend: cannot use postconf to search queue_directory, command was: \"%s\"", pconf );
		pclose ( p );
		return PFBE_UNUSABLE;
	}
	if ( !freadl ( p, queue_path, sizeof(queue_path) ) ) {
		syslog ( LOGLEVEL, "pfqueue postfix1 backend: cannot use postconf to search queue_directory, command was: \"%s\"", pconf );
		pclose ( p );
		return PFBE_UNUSABLE;
	}
	pclose ( p );

	return PFBE_OK;
}

int pfb_close() {
	return PFBE_OK;
}

int pfb_fill_queue() {
	char buf[BUF_SIZE];
	NUMMSG_THREAD = 0;
	snprintf ( buf, sizeof(buf), "%s/%s", queue_path, q_names[CURQ] );
	dir_dig(buf);
	return NUMMSG_THREAD;
}

int pfb_retr_headers( const char* msgid ) {
	FILE *p;
	char buf[BUF_SIZE];
	int f1, f2, f3, l1, l2;
	char *s1, *s2;
	struct msg_t *msg;
	int dirty;
	
	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_MSGNOTEX;

	if ( msg->hcached )
		return PFBE_MSGCACHED;

	if ( has_configpath )
		snprintf ( buf, BUF_SIZE, "%s -c %s %s 2> /dev/null", 
			postcat_path, config_path, msg->path );
	else
		snprintf ( buf, BUF_SIZE, "%s %s 2> /dev/null", 
			postcat_path, msg->path );

	p = popen ( buf, "r" );
	if ( !p ) {
		strcpy ( msg->from, PFBE_SERROR );
		strcpy ( msg->to,   PFBE_SERROR );
		msg->hcached = 0;
		return PFBE_MSGNOTEX;
	}
	
	f1 = f2 = f3 = 0;
	
	dirty = 1;
	strcpy ( msg->from, PFBE_SNOTFOUND );
	strcpy ( msg->to,   PFBE_SNOTFOUND );

	if ( pfb_using_envelope ) {
		l1 = ENVELOPE_FROM_LEN;
		l2 = ENVELOPE_TO_LEN;
		s1 = ENVELOPE_FROM;
		s2 = ENVELOPE_TO;
	} else {
		l1 = HEADER_FROM_LEN;
		l2 = HEADER_TO_LEN;
		s1 = HEADER_FROM;
		s2 = HEADER_TO;
	}

	while ( (f1==0||f2==0||f3==0) && freadl ( p, buf, BUF_SIZE ) ) {
		if ( !f1 && ( !strncmp ( buf, s1, l1 ) ) ) {
			memcpy ( msg->from, buf+l1, sizeof(msg->from) );
			if ( !strlen(msg->from) )
				strcpy ( msg->from, ENVELOPE_NULL_SENDER );
			f1++;
		}
		if ( !f2 && ( !strncmp ( buf, s2, l2 ) ) ) {
			memcpy ( msg->to, buf+l2, sizeof(msg->to) );
			f2++;
		}
		if ( !f3 && !strncmp ( buf, HEADER_SUBJECT, HEADER_SUBJECT_LEN ) ) {
			memcpy ( msg->subj, buf+HEADER_SUBJECT_LEN, sizeof(msg->subj) );
			f3++;
		}
	}
	pclose ( p );

	if ( f1 && f2 && f3 )
		dirty = 0;
	
	if ( (!dirty) && strlen(msg->to) && strlen(msg->from) )
		msg->hcached = 1;
	else
		msg->hcached = 0;
	return PFBE_OK;
}

int pfb_retr_status( const char* msgid ) {
	FILE *p;
	char buf[BUF_SIZE];
	char buf2[BUF_SIZE];
	char *c;
	struct msg_t *msg;
	
	msg = msg_from_id(msgid);
	if ( !msg )
		return 2;

	if ( msg->scached )
		return 1;
	
	if ( CURQ == Q_DEFERRED ) {
		// replace 'deferred' with 'defer'
		c = strstr ( msg->path, "deferred" );
		if ( c ) {
			memset ( buf, 0, sizeof(buf) );
			strncpy ( buf, msg->path, c - msg->path );
			sprintf ( buf2, "%sdefer%s", buf, c+8 );
		}
		p = fopen ( buf2, "r" );
		if ( !p )
			strcpy ( msg->stat, "Deferred, no reason" );
		else {
			freadl ( p, msg->stat, sizeof(msg->stat) );
		}

		if ( p )
			fclose ( p );
	} else if ( CURQ == Q_ACTIVE )
		strcpy ( msg->stat, "Active" );
	else if ( CURQ == Q_HOLD )
		strcpy ( msg->stat, "Held" );
	else if ( CURQ == Q_INCOMING )
		strcpy ( msg->stat, "Incoming" );
	msg->scached = 1;
	return 1;
}


int pfb_retr_body( const char* msgid, char *buffer, size_t buflen ) {
	char b[BUF_SIZE];
	int j;
	FILE *p;
	struct msg_t *msg;

	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_MSGNOTEX;

	if ( has_configpath )
		snprintf ( b, BUF_SIZE, "%s -c %s %s 2> /dev/null", 
			postcat_path, config_path, msg->path );
	else
		snprintf ( b, BUF_SIZE, "%s %s 2> /dev/null", 
			postcat_path, msg->path );
	
	p = popen ( b, "r" );
	if ( !p )
		return PFBE_MSGNOTEX;
	j = fread( buffer, sizeof(char), buflen, p );
	pclose ( p );
	return j;
	
}

int pfb_action(int act, const char* msg) {
char b[BUF_SIZE];
char o;

	switch ( act ) {
	case MSG_DELETE:
		o = 'd';
		break;
	case MSG_HOLD:
		o = 'h';
		break;
	case MSG_RELEASE:
		o = 'H';
		break;
	case MSG_REQUEUE:
		o = 'r';
		break;
	default:
		return 1;
	}
	if ( has_configpath )
		snprintf ( b, BUF_SIZE, "%s -c %s -%c %s 2>/dev/null", 
			postsuper_path, config_path, o, msg );
	else
		snprintf ( b, BUF_SIZE, "%s -%c %s 2>/dev/null", 
			postsuper_path, o, msg );
	system ( b );
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
	CURQ = q;
	return PFBE_OK;
}

void pfb_use_envelope ( int u ) {
	pfb_using_envelope = u;
}

int pfb_get_caps() {
	return pfb_caps;
}

char* pfb_queue_name ( int q ) {
	if ( q<=4 )
		return q_names[q];
	else
		return "noname";
}

int pfb_queue_count() {
	return 4;
}

