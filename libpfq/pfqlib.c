
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>

#include "../pfqlib.h"
#include "pfqlib_priv.h"
#include "../config.h"
#include "../pfregex.h"
#include "../pfqmessage.h"
#include "../backends/pfq_backend.h"

int dig_suspend;

#define TH_UNINITIALIZED -1
#define TH_RUNNABLE       0
#define TH_STOPRQ         1
#define TH_STOPPED        2
int thread_control;

int tmp_sort_sense;

// Compare callbacks for qsort
int msg_compare_from ( const void *m1, const void *m2 ) {
	return tmp_sort_sense * strcmp ( ((struct msg_t*)m1)->from, ((struct msg_t*)m2)->from );
}

int msg_compare_to ( const void *m1, const void *m2 ) {
	return tmp_sort_sense * strcmp ( ((struct msg_t*)m1)->to, ((struct msg_t*)m2)->to );
}

int msg_compare_subject ( const void *m1, const void *m2 ) {
	return tmp_sort_sense * strcmp ( ((struct msg_t*)m1)->subj, ((struct msg_t*)m2)->subj );
}

struct pfql_status_t* pfql_getstatus( struct pfql_context_t *ctx ) {
	return &ctx->pfql_status;
}

struct pfql_conf_t* pfql_getconf( struct pfql_context_t *ctx) {
	return &ctx->pfql_conf;
}

const char* pfql_version() {
	return PFQL_VERSION;
}

const char* pfql_queue_name( struct pfql_context_t *ctx, int i) {
	return ctx->pfqbe_queue_name(i);
}

void pfql_queue_sort ( struct pfql_context_t *ctx ) {
	dig_suspend = 1;
	tmp_sort_sense = ctx->pfql_status.sort_sense;
	if ( ctx->pfql_status.sort_field==PFQL_SORT_FROM )
		qsort ( ctx->queue, ctx->NUMMSG, sizeof(struct msg_t), msg_compare_from );
	if ( ctx->pfql_status.sort_field==PFQL_SORT_TO )
		qsort ( ctx->queue, ctx->NUMMSG, sizeof(struct msg_t), msg_compare_to );
	if ( ctx->pfql_status.sort_field==PFQL_SORT_SUBJECT )
		qsort ( ctx->queue, ctx->NUMMSG, sizeof(struct msg_t), msg_compare_subject );
	dig_suspend = 0;
}

time_t pfql_queue_last_changed( struct pfql_context_t *ctx ) {
	return ctx->queue_last_changed;
}

int pfql_backend_apiversion( struct pfql_context_t *ctx ) {
	return ctx->pfqbe_apiversion();
}

const char* pfql_backend_id( struct pfql_context_t *ctx ) {
	return ctx->pfqbe_id();
}

const char* pfql_backend_version( struct pfql_context_t *ctx ) {
	return ctx->pfqbe_version();
}

const char* pfql_backend_path(struct pfql_context_t *ctx) {
	return ctx->pfqbe_path();
}

int pfql_retr_status( struct pfql_context_t *ctx, const char *id ) {
	return ctx->pfqbe_retr_status(id);
}

int pfql_retr_headers( struct pfql_context_t *ctx, const char *id ) {
	return ctx->pfqbe_retr_headers(id);
}

int pfql_retr_body( struct pfql_context_t *ctx, const char *id, void* buf, size_t t) {
int res;
	res = ctx->pfqbe_retr_body(id,buf,t);
	if ( res != PFBE_MSGNOTEX )
		return res;
	else
		return PFQL_MSGNOTEX;
}

int pfql_msg_getpos( struct pfql_context_t *ctx, const char* id) {
int i;
	if ( !ctx->NUMMSG )
		return PFQL_MSGNOTEX;
	for ( i=0; i<ctx->NUMMSG; i++ ) {
		if ( !strcmp(id, ctx->queue[i].id ) )
			return i;
	}
	return PFQL_MSGNOTEX;
}

int pfql_num_msg( struct pfql_context_t *ctx ) {
	return ctx->NUMMSG;
}

int pfql_num_tag( struct pfql_context_t *ctx ) {
	return ctx->NUMTAG;
}

void pfql_msg_tag( struct pfql_context_t *ctx, const char* id) {
int i;

	i = pfql_msg_getpos(ctx,id);
	if ( i==-1 )
		return;

	if ( !ctx->queue[i].tagged ) {
		ctx->queue[i].tagged = 1;
		ctx->NUMTAG++;
	}
}

void pfql_msg_untag( struct pfql_context_t *ctx, const char* id) {
int i;
	i = pfql_msg_getpos(ctx,id);
	if ( i==-1 )
		return;
	
	if ( ctx->queue[i].tagged ) {
		ctx->queue[i].tagged = 0;
		ctx->NUMTAG--;
	}
}

void pfql_msg_toggletag( struct pfql_context_t *ctx, const char* id) {
int i;
	i = pfql_msg_getpos(ctx,id);
	if ( i==-1 )
		return;
	
	if ( ctx->queue[i].tagged )
		pfql_msg_untag(ctx,id);
	else
		pfql_msg_tag(ctx,id);

}

int pfql_msg_istagged( struct pfql_context_t *ctx, const char* id) {
int i;
	i = pfql_msg_getpos(ctx, id);
	if ( i==-1 )
		return 0;
	return ctx->queue[i].tagged;
}

struct msg_t *pfql_msg_at( struct pfql_context_t *ctx, int i) {
	if ( i<ctx->NUMMSG )
		return &ctx->queue[i];
	else
		return NULL;
}

struct msg_t *pfql_msg( struct pfql_context_t *ctx, const char* id) {
int i;
	i = pfql_msg_getpos(ctx, id);
	if ( i==-1 )
		return NULL;
	else
		return &ctx->queue[i];
}

// Threaded loop
void* queue_fill_thread(void *arg) {
	int NUMMSG_NEW;
	int i;
	int b;
	
	struct pfql_context_t *ctx = (struct pfql_context_t*)arg;

	while ( thread_control == TH_RUNNABLE ) {
		
		ctx->pfql_status.queue_status = PFQL_Q_FILLING;

		if ( !dig_suspend && ctx->pfql_status.do_scan ) {
			if ( dig_limit )
				dig_start = time(NULL);
			NUMMSG_NEW = ctx->pfqbe_fill_queue();
			b = 0;
			if ( NUMMSG_NEW != ctx->NUMMSG )
				b = 1;
			ctx->NUMMSG = NUMMSG_NEW;
			for ( i=0; i<NUMMSG_NEW; i++ ) {
				if ( ctx->queue_thread[i].changed ) {
					memcpy ( ctx->queue[i].id, ctx->queue_thread[i].id, sizeof(ctx->queue[i].id) );
					memcpy ( ctx->queue[i].path, ctx->queue_thread[i].path, sizeof(ctx->queue[i].path) );
					ctx->queue[i].hcached = 0;
					ctx->queue[i].scached = 0;
					ctx->queue[i].tagged  = 0;
					b = 1;

					if ( ctx->pfql_status.sort_field )
						pfql_retr_headers ( ctx, ctx->queue[i].id );
				}
			}
			if ( b )
				ctx->queue_last_changed = time(NULL);
			ctx->dig_lastqueue = ctx->pfql_status.cur_queue;
		}
		if ( ctx->pfql_status.sort_field!=PFQL_SORT_UNSORTED ) {
			ctx->pfql_status.queue_status = PFQL_Q_SORTING;
			pfql_queue_sort ( ctx ); 
			ctx->pfql_status.queue_status = PFQL_Q_IDLE;
		}

		sleep ( ctx->pfql_conf.scan_delay );
	}
	pthread_mutex_unlock ( &ctx->qfill_mutex );
	thread_control = TH_STOPPED;
	pthread_exit(NULL);
}

// Launches the dig thread
int queue_fill_start(struct pfql_context_t* ctx) {
	if ( pthread_mutex_trylock(&ctx->qfill_mutex)!=0 )
		return PFQL_ERROR;
	thread_control = TH_RUNNABLE;
	pthread_create ( &ctx->qfill_thread, NULL, queue_fill_thread, ctx );
	return PFQL_OK;
}

// Stops the dig thread
int queue_fill_stop() {
	if ( thread_control != TH_UNINITIALIZED )
		thread_control = TH_STOPRQ;
	while ( thread_control != TH_STOPPED && thread_control != TH_UNINITIALIZED )
		usleep ( 200000 );
	return 0;
}

// Performs an action on the message
void msg_action_do ( struct pfql_context_t *ctx, const char* id, int act ) {

	switch ( act ) {
	case MSG_DELETE:
		ctx->pfqbe_message_delete ( id );
		break;
	case MSG_HOLD:
		ctx->pfqbe_message_hold ( id );
		break;
	case MSG_RELEASE:
		ctx->pfqbe_message_release ( id );
		break;
	case MSG_REQUEUE:
		ctx->pfqbe_message_requeue ( id );
		break;
	default:
		return;
	}
}

// Tags all messages
void pfql_tag_all( struct pfql_context_t *ctx ) {
int i;
	for ( i=0; i<ctx->NUMMSG; i++ )
		ctx->queue[i].tagged = TRUE;
	ctx->NUMTAG = ctx->NUMMSG;
}

// Resets tag flag on all messages
void pfql_tag_none( struct pfql_context_t *ctx ) {
int i;
	for ( i=0; i<ctx->NUMMSG; i++ )
		ctx->queue[i].tagged = FALSE;
	ctx->pfql_status.wrk_tagged = FALSE;
	ctx->NUMTAG = 0;
}

// Reset the cache flag of the messages
void msg_cachereset( struct pfql_context_t *ctx ) {
int i;
	for ( i=0; i<ctx->NUMMSG; i++ )
		ctx->queue[i].hcached = 0;
}

// Wrapper
void pfql_msg_action ( struct pfql_context_t *ctx, const char *id, int act ) {
int i;

	if ( (ctx->pfql_status.wrk_tagged) || (ctx->pfql_status.auto_wrk_tagged && ctx->NUMTAG) ) {
		dig_suspend = 1;
		for ( i = 0; i<ctx->NUMMSG; i++ ) {
			if ( ctx->queue[i].tagged )
				msg_action_do ( ctx, ctx->queue[i].id, act );
		}
		pfql_tag_none(ctx);
		dig_suspend = 0;
	} else {
		i = pfql_msg_getpos(ctx,id);
		if ( i==-1 )
			return;
		msg_action_do ( ctx, ctx->queue[i].id, act );
	}
	return;
}

// Clears the queue
void queue_reset( struct pfql_context_t *ctx ) {
	memset ( ctx->queue, 0, sizeof(struct msg_t)*ctx->pfql_conf.msg_max );
}

int pfql_num_queues( struct pfql_context_t *ctx ) {
	return ctx->pfqbe_queue_count();
}

// Changes the current queue
int pfql_set_queue( struct pfql_context_t *ctx, int q ) {

	if ( q >= ctx->pfqbe_queue_count() )
		return PFQL_ERROR;
	
	ctx->pfql_status.cur_queue = q;
	ctx->NUMTAG = 0;
	ctx->pfql_status.wrk_tagged = FALSE;
	queue_reset(ctx);
	ctx->queue_last_changed = time(NULL);
	
	ctx->pfqbe_set_queue ( q );

	// Ensure that a scan is made before proceeding, loop 1/5 sec
	while ( ctx->dig_lastqueue != ctx->pfql_status.cur_queue ) { usleep ( 200000 ); };
	
	return PFQL_OK;
}

// Search for a message; returns -1 if not found
int msg_match( struct pfql_context_t *ctx, int reset, int direction) {
int i, res;
static int pos;

	if ( reset )
		pos = -1;
	if ( direction==0 )
		pos++;
	else
		pos--;
	if ( pos<0 )
		return -1;

	res = 0;

	if ( direction==0 ) {
		for ( i=pos; i<ctx->NUMMSG; i++ ) {
			ctx->pfqbe_retr_headers ( ctx->queue[i].id );
			if ( ( (ctx->search_mode & SM_FROM) && regexec(ctx->regexp,ctx->queue[i].from,res,NULL,0)==0 ) ||
			     ( (ctx->search_mode & SM_TO)   && regexec(ctx->regexp,ctx->queue[i].to  ,res,NULL,0)==0 ) ||
			     ( (ctx->search_mode & SM_SUBJ) && regexec(ctx->regexp,ctx->queue[i].subj,res,NULL,0)==0 ) ) {
				pos = i;
				return i;
			}
		}
	} else {
		for ( i=pos; i>=0; i-- ) {
			ctx->pfqbe_retr_headers ( ctx->queue[i].id );
			if ( ( (ctx->search_mode & SM_FROM) && regexec(ctx->regexp,ctx->queue[i].from,res,NULL,0)==0 ) ||
			     ( (ctx->search_mode & SM_TO)   && regexec(ctx->regexp,ctx->queue[i].to  ,res,NULL,0)==0 ) ||
			     ( (ctx->search_mode & SM_SUBJ) && regexec(ctx->regexp,ctx->queue[i].subj,res,NULL,0)==0 ) ) {
				pos = i;
				return i;
			}
		}
	}
	return -1;
}

int pfql_msg_search( struct pfql_context_t *ctx, const char* regexps) {
int res;
	res = regcomp ( ctx->regexp, regexps, 0 );
	if ( !res ) 
		return msg_match(ctx,1,0);
	else
		return PFQL_INVREGEXP;
}

int pfql_msg_searchnext( struct pfql_context_t *ctx, const char* regexps) {
int res;
	res = regcomp ( ctx->regexp, regexps, 0 );
	if ( !res ) 
		return msg_match(ctx,0,0);
	else
		return PFQL_INVREGEXP;
}

int pfql_msg_searchprev( struct pfql_context_t *ctx, const char* regexps) {
int res;
	res = regcomp ( ctx->regexp, regexps, 0 );
	if ( !res ) 
		return msg_match(ctx,0,1);
	else
		return PFQL_INVREGEXP;
}

void pfql_msg_searchandtag ( struct pfql_context_t *ctx, const char* regexps) {
int res;

	res = regcomp ( ctx->regexp, regexps, 0 );
	if ( !res ) {
		res = msg_match(ctx,1,0);
		while ( res != -1 ) {
			ctx->queue[res].tagged = 1;
			ctx->NUMTAG++;
			res = pfql_msg_searchnext(ctx,regexps);
		}
	}
}

// Toggle envelope usage for from/to
void pfql_toggle_envelope(struct pfql_context_t *ctx) {
	if ( !(ctx->pfqbe_get_caps() & BECAPS_MSG_ENVELOPE) )
		return;

	ctx->pfql_status.use_envelope = !ctx->pfql_status.use_envelope;
	msg_cachereset(ctx);
	ctx->pfqbe_use_envelope ( ctx->pfql_status.use_envelope );
}

int be_load ( struct pfql_context_t *ctx, const char* be ) {
	char buf[BUF_SIZE];
	
	syslog ( LOGLEVEL, "pfqlib: looking for backend '%s'", be );

	if ( strlen(ctx->pfql_conf.backends_path) )
		sprintf ( buf, "%s/libpfq_%s.so.%s", ctx->pfql_conf.backends_path, be, PFQ_SONAME );
	else
		sprintf ( buf, "%s/libpfq_%s.so.%s", PFBEDIR, be, PFQ_SONAME );
	
	syslog ( LOGLEVEL, "pfqlib: trying to open %s", buf );

	ctx->beptr = dlopen ( buf, RTLD_LAZY );
	if ( !ctx->beptr ) {
		syslog ( LOGLEVEL, "pfqlib: failed, %s", dlerror() );
		
		// Try 'pfqueue' subdir
		if ( strlen(ctx->pfql_conf.backends_path) )
			sprintf ( buf, "%s/pfqueue/libpfq_%s.so", ctx->pfql_conf.backends_path, be );
		else
			sprintf ( buf, "pfqueue/libpfq_%s.so", be );
		ctx->beptr = dlopen ( buf, RTLD_LAZY );
		syslog ( LOGLEVEL, "pfqlib: trying to open %s", buf );

	}
	if ( !ctx->beptr ) {
		syslog ( LOGLEVEL, "pfqlib: failed, %s", dlerror() );
		return PFQL_BENOTFOUND;
	}
	
	syslog ( LOGLEVEL, "pfqlib: backend opened" );
	ctx->pfqbe_apiversion = dlsym( ctx->beptr, "pfb_apiversion" );
	if ( !ctx->pfqbe_apiversion ) {
		syslog ( LOGLEVEL, "pfqlib: missing pfb_apiversion symbol, cannot use backend" );
		return PFQL_BEMISSINGSYM;
	}

	syslog ( LOGLEVEL, "pfqlib: API version is %d", ctx->pfqbe_apiversion() );
	if ( ctx->pfqbe_apiversion()!=PFQL_MINBEVERSION ) {
		syslog ( LOGLEVEL, "pfqlib: wrong API version, %d is required", PFQL_MINBEVERSION );
		return PFQL_BEWRONGAPI;
	}

	ctx->pfqbe_init  = dlsym( ctx->beptr, "pfb_init" );
	if ( !ctx->pfqbe_init )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_close  = dlsym( ctx->beptr, "pfb_close" );
	if ( !ctx->pfqbe_close )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_id  = dlsym( ctx->beptr, "pfb_id" );
	if ( !ctx->pfqbe_id )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_version  = dlsym( ctx->beptr, "pfb_version" );
	if ( !ctx->pfqbe_version )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_setup = dlsym( ctx->beptr, "pfb_setup" );
	if ( !ctx->pfqbe_setup )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_fill_queue = dlsym( ctx->beptr, "pfb_fill_queue" );
	if ( !ctx->pfqbe_fill_queue )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_retr_headers = dlsym( ctx->beptr, "pfb_retr_headers" );
	if ( !ctx->pfqbe_retr_headers )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_retr_status = dlsym( ctx->beptr, "pfb_retr_status" );
	if ( !ctx->pfqbe_retr_status )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_retr_body = dlsym( ctx->beptr, "pfb_retr_body" );
	if ( !ctx->pfqbe_retr_body )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_message_delete = dlsym( ctx->beptr, "pfb_message_delete" );
	if ( !ctx->pfqbe_message_delete )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_message_hold = dlsym( ctx->beptr, "pfb_message_hold" );
	if ( !ctx->pfqbe_message_hold )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_message_release = dlsym( ctx->beptr, "pfb_message_release" );
	if ( !ctx->pfqbe_message_release )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_message_requeue = dlsym( ctx->beptr, "pfb_message_requeue" );
	if ( !ctx->pfqbe_message_requeue )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_set_queue = dlsym( ctx->beptr, "pfb_set_queue" );
	if ( !ctx->pfqbe_set_queue )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_use_envelope = dlsym( ctx->beptr, "pfb_use_envelope" );
	if ( !ctx->pfqbe_use_envelope )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_get_caps = dlsym( ctx->beptr, "pfb_get_caps" );
	if ( !ctx->pfqbe_get_caps )
		return PFQL_BEMISSINGSYM;
	
	ctx->pfqbe_queue_name = dlsym( ctx->beptr, "pfb_queue_name" );
	if ( !ctx->pfqbe_queue_name )
		return PFQL_BEMISSINGSYM;
	
	ctx->pfqbe_queue_count = dlsym( ctx->beptr, "pfb_queue_count" );
	if ( !ctx->pfqbe_queue_count )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_getconf = dlsym( ctx->beptr, "pfb_getconf" );
	if ( !ctx->pfqbe_queue_count )
		return PFQL_BEMISSINGSYM;

	/* Backend version 4 */

	ctx->pfqbe_set_path = dlsym( ctx->beptr, "pfb_set_path" );
	if ( !ctx->pfqbe_set_path )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_path = dlsym( ctx->beptr, "pfb_path" );
	if ( !ctx->pfqbe_path )
		return PFQL_BEMISSINGSYM;

	ctx->pfqbe_set_path ( buf );

	syslog ( LOGLEVEL, "pfqlib: backend %s is OK", buf );
	return PFQL_OK;
}

int be_try ( struct pfql_context_t *ctx, const char *b ) {
	int res;

	res = be_load ( ctx, b );
	if ( res )
		return PFQL_ERROR;
	
	res = ctx->pfqbe_init ();
	if ( res ) 
		return PFQL_ERROR;

	ctx->pfqbe_getconf()->msg_max = ctx->pfql_conf.msg_max;
	ctx->pfqbe_getconf()->scan_limit = ctx->pfql_conf.scan_limit;
	res = ctx->pfqbe_setup(ctx->queue, ctx->queue_thread);
	if ( res )
		return PFQL_ERROR;

	ctx->pfqbe_close(ctx);
	
	return PFQL_OK;
}

int pfql_context_create ( struct pfql_context_t **ctx ) {
	*ctx = malloc(sizeof(struct pfql_context_t));
	if ( !ctx )
		return PFQL_MALLOC;
	else
		return PFQL_OK;
}

int pfql_init(struct pfql_context_t *ctx) {
	/* Defaults */
	ctx->pfql_conf.max_char      = 200;
	ctx->pfql_conf.initial_queue = 0;
	sprintf ( ctx->pfql_conf.backends_path, "%c", 0 );
	sprintf ( ctx->pfql_conf.frontends_path, "%c", 0 );
	sprintf ( ctx->pfql_conf.backend_name, "autodetect" );
	sprintf ( ctx->pfql_conf.frontend_name, "ncurses" );
	ctx->pfql_conf.msg_max       = 200;
	ctx->pfql_conf.scan_limit    = 0;
	ctx->pfql_conf.scan_delay    = 1;
	sprintf ( ctx->pfql_conf.remote_host, "%c", 0 );
	ctx->pfql_conf.remote_port   = 20000;
	ctx->pfql_conf.config_from   = 0;

	ctx->pfql_status.wrk_tagged      = 0;
	ctx->pfql_status.auto_wrk_tagged = 0;
	ctx->pfql_status.ask_confirm     = 1;
	ctx->pfql_status.do_scan         = 1;
	ctx->pfql_status.use_envelope    = 0;
	ctx->pfql_status.use_colors      = 1;
	ctx->pfql_status.cur_queue       = 0;
	ctx->pfql_status.sort_field      = PFQL_SORT_UNSORTED;
	ctx->pfql_status.sort_sense	 = PFQL_SORT_ASC;

	ctx->beptr = 0;
	ctx->frontend.feptr = 0;

	ctx->queue = 0;
	ctx->queue_thread = 0;
	ctx->regexp = 0;

	ctx->NUMTAG = 0;

	return PFQL_OK;
}

void pfql_backend_setconfig(struct pfql_context_t *ctx, const char* c) {
	strcpy ( ctx->pfqbe_getconf()->config_path, c );
}

void pfql_backend_setcommand(struct pfql_context_t *ctx, const char* c) {
	strcpy ( ctx->pfqbe_getconf()->command_path, c );
}

int pfql_backend_start(struct pfql_context_t *ctx) {
	int res;

	thread_control = TH_UNINITIALIZED;

	/* Alloc resources */
	ctx->regexp = (regex_t*) malloc (sizeof(regex_t));
	if ( !ctx->regexp ) {
		syslog ( LOGLEVEL, "pfqlib: sorry, cannot malloc for %d for the regex!", sizeof(regex_t) );
		return PFQL_MALLOC;
	}
	res = regcomp ( ctx->regexp, "*", 0 );

	ctx->queue = (struct msg_t*)malloc(sizeof(struct msg_t)*ctx->pfql_conf.msg_max);
	if ( !ctx->queue ) {
		regfree ( ctx->regexp );
		syslog ( LOGLEVEL, "pfqlib: sorry, cannot malloc for %d elements (queue)!", ctx->pfql_conf.msg_max );
		return PFQL_MALLOC;
	}

	ctx->beptr = 0;

	ctx->queue_thread = (struct be_msg_t*)malloc(sizeof(struct be_msg_t)*ctx->pfql_conf.msg_max);
	if ( !ctx->queue_thread ) {
		regfree ( ctx->regexp );
		free ( ctx->queue );
		syslog ( LOGLEVEL, "pfqlib: sorry, cannot malloc for %d elements (queue_thread)!", ctx->pfql_conf.msg_max );
		return PFQL_MALLOC;
	}

	if ( !strcmp ( ctx->pfql_conf.backend_name, "autodetect" ) ) {
		// Try to autodetect backend
		strcpy ( ctx->pfql_conf.backend_name, "exim" );
		res = be_try ( ctx, ctx->pfql_conf.backend_name );
		if ( res!=PFQL_OK ) {
			syslog ( LOGLEVEL, "pfqlib: backend can't be used" );
			strcpy ( ctx->pfql_conf.backend_name, "postfix2" );
			res = be_try ( ctx, ctx->pfql_conf.backend_name );
		}
		if ( res!=PFQL_OK ) {
			syslog ( LOGLEVEL, "pfqlib: backend can't be used" );
			strcpy ( ctx->pfql_conf.backend_name, "postfix1" );
			res = be_try ( ctx, ctx->pfql_conf.backend_name );
		}
		if ( res!=PFQL_OK ) {
			syslog ( LOGLEVEL, "pfqlib: backend can't be used" );
			syslog ( LOGLEVEL, "pfqlib: cannot autodetect suitable backend, try -b and/or -B option" );
			ctx->beptr = 0;
			return PFQL_NOBE;
		}
	}
	
	switch ( be_load ( ctx, ctx->pfql_conf.backend_name ) ) {
	case PFQL_BENOTFOUND:
		syslog ( LOGLEVEL, "pfqlib: backend not found!" );
		ctx->beptr = 0;
		return PFQL_BENOTFOUND;
	case PFQL_BEMISSINGSYM:
		syslog ( LOGLEVEL, "pfqlib: backend not valid (missing symbols)!" );
		ctx->beptr = 0;
		return PFQL_BEMISSINGSYM;
	}

	strcpy ( ctx->pfqbe_getconf()->host, ctx->pfql_conf.remote_host );
	ctx->pfqbe_getconf()->port = ctx->pfql_conf.remote_port;

	if ( ctx->pfqbe_init()!=PFBE_OK ) {
		syslog ( LOGLEVEL, "pfqlib: %s backend failed to init!", ctx->pfql_conf.backend_name );
		ctx->beptr = 0;
		return PFQL_BEINIT;
	}
	
	strcpy ( ctx->pfqbe_getconf()->config_path, ctx->pfql_conf.backend_config );
	strcpy ( ctx->pfqbe_getconf()->command_path,ctx->pfql_conf.backend_progs );
	ctx->pfqbe_getconf()->msg_max = ctx->pfql_conf.msg_max;
	ctx->pfqbe_getconf()->scan_limit = ctx->pfql_conf.scan_limit;
	
	if ( ctx->pfqbe_setup( ctx->queue, ctx->queue_thread )!=PFBE_OK ) {
		syslog ( LOGLEVEL, "pfqlib: %s backend failed to setup!", ctx->pfql_conf.backend_name );
		ctx->beptr = 0;
		return PFQL_BEINIT;
	}

	ctx->dig_lastqueue = -1;

	queue_fill_start( ctx );
	pfql_set_queue ( ctx, ctx->pfql_conf.initial_queue );

	return PFQL_OK;

}

int pfql_context_destroy(struct pfql_context_t *ctx) {

	pthread_mutex_destroy ( &ctx->qfill_mutex );
	queue_fill_stop(ctx);

	if (ctx->beptr) {
		ctx->pfqbe_close(ctx);
		dlclose (ctx->beptr);
	}
	if (ctx->frontend.feptr ) {
		dlclose ( ctx->frontend.feptr );
	}
	
	if ( ctx->queue )
		free ( ctx->queue );
	if ( ctx->queue_thread )
		free ( ctx->queue_thread );
	if ( ctx->regexp )
		regfree ( ctx->regexp );

	return PFQL_OK;

}

int pfql_dump ( struct pfql_context_t *ctx, const char *fname ) {
	return PFQL_OK;
}
