/*
	Functions to handle frontends
*/

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

int pfql_frontend_start ( struct pfql_context_t *ctx ) {
	if ( pfql_frontend_load ( ctx, ctx->pfql_conf.frontend_name ) == PFQL_OK )
		ctx->frontend.pfqfe_start();
	return PFQL_OK;
}

int pfql_frontend_end ( struct pfql_context_t *ctx ) {
	return PFQL_OK;
}

int pfql_frontend_load ( struct pfql_context_t *ctx, const char* fe ) {
	char buf[BUF_SIZE];
	
	syslog ( LOGLEVEL, "pfqlib: looking for frontend '%s'", fe );

	if ( strlen(ctx->pfql_conf.frontends_path) )
		sprintf ( buf, "%s/libpfq_fe%s.so.%s", ctx->pfql_conf.frontends_path, fe, PFQ_SONAME );
	else
		sprintf ( buf, "%s/libpfq_fe%s.so.%s", PFBEDIR, fe, PFQ_SONAME );
	
	syslog ( LOGLEVEL, "pfqlib: trying to open %s", buf );

	ctx->frontend.feptr = dlopen ( buf, RTLD_LAZY );
	if ( !ctx->frontend.feptr ) {
		syslog ( LOGLEVEL, "pfqlib: failed, %s", dlerror() );
		
		// Try 'pfqueue' subdir
		if ( strlen(ctx->pfql_conf.frontends_path) )
			sprintf ( buf, "%s/pfqueue/libpfq_fe%s.so", ctx->pfql_conf.frontends_path, fe );
		else
			sprintf ( buf, "pfqueue/libpfq_fe%s.so", fe );
		ctx->frontend.feptr = dlopen ( buf, RTLD_LAZY );
		syslog ( LOGLEVEL, "pfqlib: trying to open %s", buf );

	}
	if ( !ctx->frontend.feptr ) {
		syslog ( LOGLEVEL, "pfqlib: failed, %s", dlerror() );
		return PFQL_BENOTFOUND;
	}
	
	syslog ( LOGLEVEL, "pfqlib: frontend opened" );

	ctx->frontend.pfqfe_init = dlsym( ctx->frontend.feptr, "pff_init" );
	if ( !ctx->frontend.pfqfe_init ) {
		syslog ( LOGLEVEL, "pfqlib: missing pff_init symbol, cannot use frontend" );
		return PFQL_BEMISSINGSYM;
	}

	ctx->frontend.pfqfe_start = dlsym( ctx->frontend.feptr, "pff_start" );
	if ( !ctx->frontend.pfqfe_start ) {
		syslog ( LOGLEVEL, "pfqlib: missing pff_start symbol, cannot use frontend" );
		return PFQL_BEMISSINGSYM;
	}

	ctx->frontend.pfqfe_name = dlsym( ctx->frontend.feptr, "pff_name" );
	if ( !ctx->frontend.pfqfe_name ) {
		syslog ( LOGLEVEL, "pfqlib: missing pff_name symbol, cannot use frontend" );
		return PFQL_BEMISSINGSYM;
	}

	ctx->frontend.pfqfe_version = dlsym( ctx->frontend.feptr, "pff_version" );
	if ( !ctx->frontend.pfqfe_version ) {
		syslog ( LOGLEVEL, "pfqlib: missing pff_version symbol, cannot use frontend" );
		return PFQL_BEMISSINGSYM;
	}

	ctx->frontend.pfqfe_set_path = dlsym( ctx->frontend.feptr, "pff_set_path" );
	if ( !ctx->frontend.pfqfe_set_path ) {
		syslog ( LOGLEVEL, "pfqlib: missing pff_set_path symbol, cannot use frontend" );
		return PFQL_BEMISSINGSYM;
	}

	ctx->frontend.pfqfe_init ( ctx );
	ctx->frontend.pfqfe_set_path ( buf );

	syslog ( LOGLEVEL, "pfqlib: frontend %s is OK", buf );
	return PFQL_OK;
}


