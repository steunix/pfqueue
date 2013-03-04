
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

#include "pfq_backend.h"
#include "pfq_service.h"
#include "../pfqmessage.h"
#include "../config.h"

char spool_dir[BUF_SIZE];
char exim_cmd[BUF_SIZE];
char exim_conf[BUF_SIZE];

char *q_names[]={
	"input"
};

int NUMMSG_THREAD;

struct pfb_conf_t pfb_conf;

#define HEADER_FROM	"From: "
#define HEADER_TO	"To: "
#define HEADER_SUBJECT  "Subject: "

int pfb_apiversion() {
	return PFQ_API_VERSION;
}

const char* pfb_id() {
	return "exim";
}

const char* pfb_version() {
	return "1.3";
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

int dir_dig( const char* basedir ) {
	DIR *bdir;
	struct dirent *dir;
	char full_path[BUF_SIZE];
	struct be_msg_t *msg;
	int l;
	
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
			l = strlen(dir->d_name);
			if ( dir->d_name[l-1]=='H' && dir->d_name[l-2]=='-') {
				msg = &(my_queue[NUMMSG_THREAD]);
				memcpy ( msg->id, dir->d_name, l-2 );
				snprintf ( msg->path, sizeof(msg->path), "%s/%s",
					basedir, dir->d_name );
				msg->changed = strncmp (dir->d_name, ext_queue[NUMMSG_THREAD].id, strlen(dir->d_name)-2 );
				NUMMSG_THREAD++;
			}
		}
	}
	if ( bdir )
		closedir ( bdir );

	return PFBE_OK;
}

int pfb_init() {
	pfb_conf.max_char = 200;
	strcpy ( pfb_conf.command_path, "" );
	return PFBE_OK;
}

void pfb_set_path(const char* p) {
	strncpy ( pfb_conf.backend_path, p, 200 );
}

int pfb_setup ( struct msg_t* qptr1, struct be_msg_t* qptr2 ) {
	FILE *p;
	char buf[BUF_SIZE];

	msg_max = pfb_conf.msg_max;
	dig_limit = pfb_conf.scan_limit;
	ext_queue = qptr1;
	my_queue = qptr2;

	strcpy ( exim_cmd, "exim");
	strcpy ( exim_conf, "" );
	strcpy ( spool_dir, "" );
	
	pfb_caps = BECAPS_MSG_HOLD + BECAPS_MSG_DEL + BECAPS_MSG_REQUEUE + BECAPS_MIXED_QUEUE +
		BECAPS_MSG_LOG;

	if ( strlen(pfb_conf.command_path ) )
		snprintf ( exim_cmd, BUF_SIZE-1, "%s/exim", pfb_conf.command_path );
	if ( strlen(pfb_conf.config_path) )
		snprintf ( exim_conf, BUF_SIZE-1, " -C %s ", pfb_conf.config_path );

	// Try exim 3...
	snprintf ( buf, BUF_SIZE, "%s %s -bP spool_directory 2> /dev/null |cut -d'=' -f2|cut -c2-", 
			exim_cmd, exim_conf );
	p = popen ( buf, "r" );
	if ( p ) {
		freadl ( p, spool_dir, sizeof(spool_dir) );
		pclose ( p );
	}

	// or exim 4
	if ( !strlen(spool_dir) ) {
		if ( strlen(pfb_conf.command_path) )
			sprintf ( exim_cmd, "%s/exim4", pfb_conf.command_path);
		else
			strcpy ( exim_cmd, "exim4");

		snprintf ( buf, BUF_SIZE, "%s %s -bP spool_directory 2> /dev/null |cut -d'=' -f2|cut -c2-", 
			exim_cmd, exim_conf );
		p = popen ( buf, "r" );
		if ( p ) {
			freadl ( p, spool_dir, sizeof(spool_dir) );
			pclose ( p );
		}
	}
	if ( !strlen(spool_dir) ) {
		syslog ( LOGLEVEL, "exim pfqueue backend: cannot guess spool_directory" );
		return PFBE_UNUSABLE;
	}

	return PFBE_OK;
}

int pfb_close() {
	return PFBE_OK;
}

int pfb_fill_queue() {
	char pbuf[BUF_SIZE];
	NUMMSG_THREAD = 0;
	snprintf ( pbuf, BUF_SIZE, "%s/input", spool_dir );
	dir_dig( spool_dir );
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
	
	snprintf ( buf, BUF_SIZE, "%s %s -Mvh %s 2> /dev/null", 
		exim_cmd, exim_conf, msg->id );

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

	l1 = strlen(HEADER_FROM);
	l2 = strlen(HEADER_TO);
	s1 = HEADER_FROM;
	s2 = HEADER_TO;

	while ( !msg->hcached && (f1==0||f2==0||f3==0) && freadl ( p, buf, BUF_SIZE ) ) {
		if ( !f1 && ( !strncmp ( buf+5, s1, l1 ) ) ) {
			memcpy ( msg->from, buf+l1+5, sizeof(msg->from) );
			if ( !strlen(msg->from) )
				strcpy ( msg->from, "Null sender" );
			f1++;
		}
		if ( !f2 && ( !strncmp ( buf+5, s2, l2 ) ) ) {
			memcpy ( msg->to, buf+l2+5, sizeof(msg->to) );
			f2++;
		}
		if ( !f3 && !strncmp ( buf+5, HEADER_SUBJECT, strlen(HEADER_SUBJECT) ) ) {
			memcpy ( msg->subj, buf+strlen(HEADER_SUBJECT)+5, sizeof(msg->subj) );
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
	int f1;
	struct msg_t *msg;
	
	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_MSGNOTEX;
	
	snprintf ( buf, BUF_SIZE, "%s %s -Mvh %s 2> /dev/null", 
		exim_cmd, exim_conf, msg->id );

	p = popen ( buf, "r" );
	if ( !p ) {
		strcpy ( msg->stat, "cant popen" );
		return PFBE_MSGNOTEX;
	}
	
	f1 = 0;
	
	strcpy ( msg->stat, "Active" );
	while ( f1==0 && freadl ( p, buf, BUF_SIZE ) ) {
		if ( !strncmp ( buf, "-frozen", 7 ) )
			strcpy ( msg->stat, "Frozen" );
	}
	pclose ( p );
	msg->scached = 0;

	return PFBE_OK;
}
int pfb_retr_body( const char* msgid, char *buffer, size_t buflen ) {
	char b[BUF_SIZE];
	int j;
	FILE *p;
	struct msg_t *msg;

	msg = msg_from_id(msgid);
	if ( !msg )
		return PFBE_MSGNOTEX;
	
	snprintf ( b, BUF_SIZE, "%s %s -Mvb %s 2> /dev/null", 
		exim_cmd, exim_conf, msg->id );
	
	p = popen ( b, "r" );
	if ( !p )
		return PFBE_MSGNOTEX;

	// Skip first line
	freadl ( p, b, sizeof(b) );
	j = fread( buffer, sizeof(char), buflen, p );
	pclose ( p );
	return j;
}

int pfb_action(int act, const char* msg) {
char b[BUF_SIZE];
char buf[BUF_SIZE];

	switch ( act ) {
	case MSG_DELETE:
		strcpy ( b, "-Mrm" );
		break;
	case MSG_HOLD:
		strcpy ( b, "-Mf" );
		break;
	case MSG_RELEASE:
		strcpy ( b, "-Mt" );
		break;
	case MSG_REQUEUE:
		strcpy ( b, "-M" );
		break;
	default:
		return -1;
	}

	snprintf ( buf, BUF_SIZE, "%s %s %s %s > /dev/null",
			exim_cmd, exim_conf, b, msg );
	system ( buf );
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
	if ( q<pfb_queue_count() )
		return PFBE_OK;
	else
		return PFBE_ERROR;
}

void pfb_use_envelope ( int u ) {
}

int pfb_get_caps() {
	return pfb_caps;
}

char* pfb_queue_name ( int q ) {
	if ( q<pfb_queue_count() )
		return q_names[q];
	else
		return "noname";
}

int pfb_queue_count() {
	return 1;
}

