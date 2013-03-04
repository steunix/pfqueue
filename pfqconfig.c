
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pfqlib.h"
#include "pfqconfig.h"

#define CFG_MAXLEN 200

void clean_row ( char *b, size_t l ) {
	int i, j;
	char *b2;
	j = 0;
	
	b2 = (char*)malloc(l);
	bzero ( b2, l );
	for ( i=0; i<l; i++ ) {
		if ( *(b+i)>' ' ) {
			*(b2+j) = *(b+i);
			j++;
		}
	}
	strcpy ( b, b2 );
	free ( b2 );
}

int read_row ( FILE *fp, char *b, size_t l ) {
	int res;
	bzero ( b, l );
	if ( fgets ( b, l, fp ) )
		return 1;
	else
		return 0;
}

void handle_row ( char *r, struct pfql_context_t *ctx ) {
	char *opt, *val, *tmp;
	struct pfql_conf_t *conf;
	long l;
	
	conf = &ctx->pfql_conf;
	opt = (char*)malloc(CFG_MAXLEN);
	val = (char*)malloc(CFG_MAXLEN);
	
	bzero ( opt, CFG_MAXLEN );
	bzero ( val, CFG_MAXLEN );
	
	tmp = strtok ( r, "=" );
	if ( tmp )
		strcpy ( opt, tmp );
	tmp = strtok ( NULL, "=" );
	if ( tmp )
		strcpy ( val, tmp );
		
	if ( opt && val ) {
		if ( !strcmp(opt,"backends_path") )
			strcpy ( conf->backends_path, val );
		if ( !strcmp(opt,"backend_name") )
			strcpy ( conf->backend_name, val );
		if ( !strcmp(opt,"frontends_path") )
			strcpy ( conf->frontends_path, val );
		if ( !strcmp(opt,"frontend_name") )
			strcpy ( conf->frontend_name, val );
		if ( !strcmp(opt,"mta_config") )
			strcpy ( conf->backend_config, val );
		if ( !strcmp(opt,"mta_bin") )
			strcpy ( conf->backend_progs, val );
		if ( !strcmp(opt,"max_messages") ) {
			l = atol(val);
			if ( l )
				conf->msg_max = l;
		}
		if ( !strcmp(opt,"scan_delay") ) {
			l = atol(val);
			if ( l )
				conf->scan_delay = l;
		}
		if ( !strcmp(opt,"scan_limit") ) {
			l = atol(val);
			if ( l )
				conf->scan_limit = l;
		}
		if ( !strcmp(opt,"use_envelope") ) {
			if ( !strcmp(val,"yes") )
				ctx->pfql_status.use_envelope = 1;
			if ( !strcmp(val,"no") )
				ctx->pfql_status.use_envelope = 0;
		}
		if ( !strcmp(opt,"default_queue") ) {
			l = atol(val);
			if ( l>=0 )
				ctx->pfql_status.cur_queue = l;
		}
		if ( !strcmp(opt,"use_colors") ) {
			if ( !strcmp(val,"yes") )
				ctx->pfql_status.use_colors = 1;
			if ( !strcmp(val,"no") )
				ctx->pfql_status.use_colors = 0;
		}
		if ( !strcmp(opt,"remote_host") ) {
			strcpy ( conf->remote_host, val );
		}
	}
		
	free ( opt );
	free ( val );
}

int pfq_read_file ( struct pfql_context_t *ctx, const char* fname ) {
	FILE *cfg;
	char *row;
	int ret;

	ret = 0;
	cfg = fopen ( fname, "r" );
	if ( cfg == 0 )
		return;

	row = (char*)malloc(CFG_MAXLEN);
	while ( read_row ( cfg, row, CFG_MAXLEN ) ) {
		clean_row ( row, CFG_MAXLEN );
		if ( row[0]!='#' ) {
			handle_row ( row, ctx );
			ret = 1;
		}
	}
	free(row);
	fclose ( cfg );
}

int pfq_read_config ( struct pfql_context_t *ctx ) {
	char *b;
	int res;

	pfql_getconf(ctx)->config_from = 0;

	res = 0;
	b = (char*)malloc(CFG_MAXLEN);
	if ( pfq_read_file ( ctx, "/etc/pfqueue.conf" ) ) {
		pfql_getconf(ctx)->config_from += 1;
		res = 1;
	}

	sprintf ( b, "%s/.pfqueue", getenv("HOME") );
	if ( pfq_read_file ( ctx, b ) ) {
		pfql_getconf(ctx)->config_from += 2;
		res += 2;
	}

	free ( b );
}
