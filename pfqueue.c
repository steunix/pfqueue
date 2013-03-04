
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <stdio.h>

#include "config.h"
#include "pfqconfig.h"
#include "pfqlib.h"

struct pfql_context_t *pfql_ctx;

void version() {
	printf ( "pfqueue - Version %s - (C) 2004-2009 Stefano Rivoir\n",
			VERSION );
}

void usage() {
	version();
	printf ( "Usage: pfqueue [-ehvn] [-q queue_num] [-m max_msg]\n"
		 "       [-b postfix1|postfix2|exim|socket] [-B backends_path]\n"
		 "       [-f ncurses|socket] [-F frontends_path]\n"
		 "       [-s autorefresh_seconds] [-l limit_seconds]\n"
		 "       [-p executables_path] [-c config_path]\n" );
}


int main ( int argc, char** argv ) {
	int opt;
	int mm;
	int st;

	struct pfql_conf_t *conf;

	if ( getuid()!=0 ) {
		fprintf ( stderr, "You need to be root to use pfqueue, sorry\n" );
		exit (-3);
	}

	pfql_ctx = 0;
	if ( pfql_context_create (&pfql_ctx)!=PFQL_OK ) {
		fprintf ( stderr, "pfqueue: cannot pfql_create_context!\n" );
		exit (-1);
	}

	if ( pfql_init(pfql_ctx)!=PFQL_OK ) {
		fprintf ( stderr, "pfqueue: cannot pfql_init!\n" );
		exit (-1);
	}
	
	/*
	if ( !fe_init() ) {
		pfql_context_destroy(pfql_ctx);
		fe_close();
		fprintf ( stderr, "pfqueue: cannot fe_init!\n" );
		exit (-2);
	}
	*/

	/* Ignore pipes error */
	signal ( SIGPIPE, SIG_IGN );

	/*
	 * Parse parameters
	 */

	// Some defaults
	pfql_ctx->regexp = 0;

	/*
	half_delay_time = 1;
	show_body_win = 0;
	show_body_always = 1;
	*/

	pfq_read_config ( pfql_ctx );

	conf = pfql_getconf(pfql_ctx);

	opt = 0;
	while ( opt!=-1 ) {
		opt = getopt ( argc, argv, "p:c:r:B:F:b:f:q:s:m:l:ehvn" );
		switch (opt) {
			case 'p':
				snprintf ( conf->backend_progs, conf->max_char, "%s", optarg );
				break;
			case 'c':
				snprintf ( conf->backend_config, conf->max_char, "%s", optarg );
				break;
			case 'r':
				snprintf ( conf->remote_host, conf->max_char, "%s", optarg );
				break;
			case 'B':
				snprintf ( conf->backends_path, conf->max_char, "%s", optarg );
				break;
			case 'F':
				snprintf ( conf->frontends_path, conf->max_char, "%s", optarg );
				break;
			case 'b':
				snprintf ( conf->backend_name, conf->max_char, "%s", optarg );
				break;
			case 'f':
				snprintf ( conf->frontend_name, conf->max_char, "%s", optarg );
				break;
			case 'q':
				mm = atoi ( optarg );
				switch ( mm ) {
				case 1:
					mm = 0;
					break;
				case 2:
					mm = 3;
					break;
				case 3:
					mm = 2;
					break;
				case 4:
					mm = 1;
					break;
				default:
					mm = 0;
				}
				pfql_getconf(pfql_ctx)->initial_queue = mm;
				break;
			case 'h':
				usage();
				exit(0);
				break;
			case 'v':
				version();
				exit(0);
				break;
			case 'm':
				mm = atoi (optarg);
				if ( mm < 5 )
					mm = 5;
				pfql_getconf(pfql_ctx)->msg_max = mm;
				break;
			case 's':
				/*
				half_delay_time = atoi ( optarg );
				if ( half_delay_time > 300 )
					half_delay_time = 300;
				if ( half_delay_time < 1 )
					half_delay_time = 1;
				*/
				break;
			case 'l':
				mm = atoi ( optarg );
				if ( mm > 300 )
					mm = 300;
				if ( mm < 1 )
					mm = 1;
				pfql_getconf(pfql_ctx)->scan_limit = mm;
				break;
			case 'e':
				pfql_getstatus(pfql_ctx)->use_envelope = 1;
				break;
			case 'n':
				pfql_getstatus(pfql_ctx)->use_colors = 0;
				break;
			case 'd':
				pfql_getconf(pfql_ctx)->scan_delay = atoi(optarg);
				break;
			case '?':
				goto do_exit;
		}
	}

	st = pfql_backend_start(pfql_ctx);	
	if ( st != PFQL_OK ) {
		fprintf ( stderr, "pfqueue: cannot start pfqlib, error %d. See syslog for details\n", st );
		pfql_context_destroy(pfql_ctx);
		exit (-4);
	}
	
	st = pfql_frontend_start(pfql_ctx);
	if ( st != PFQL_OK )
		fprintf ( stderr, "pfqueue: cannot start frontend, error %d. See syslog for details\n", st );
	else
		pfql_frontend_end(pfql_ctx);

	/*
	win_resize();
	queue_show();
	main_loop();
	*/

do_exit:
	if ( pfql_ctx )
		pfql_context_destroy ( pfql_ctx );
	/*
	fe_close();
	endwin();
	*/

	return 0;
}
