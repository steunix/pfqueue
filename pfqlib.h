
/*
 * pfqueue library interface
 */

#ifndef __PFQLIB_H
#define __PFQLIB_H

#include <time.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "pfregex.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PFQL_VERSION "1.5"

#define PFQL_BUF 200

// Structure for configuration
struct pfql_conf_t {
	int   max_char;			/* Max char buffer length 				*/
	short initial_queue;		/* Queue to start with 					*/
	char  backends_path[PFQL_BUF];	/* Path to backends 					*/
	char  backend_name[PFQL_BUF];	/* Backend to use 					*/
	char  backend_config[PFQL_BUF];	/* Backend configuration path 				*/
	char  backend_progs[PFQL_BUF];	/* Backend bin path 					*/
	int   msg_max;			/* Maximum number of messages 				*/
	int   scan_limit;		/* Max secs for a single scan 				*/
	int   scan_delay;		/* Secs between scans 					*/
	char  remote_host[PFQL_BUF];	/* Remote host (for socket backend only) 		*/
	int   remote_port;		/* Remote port (for socket backend only) 		*/
	int   config_from;		/* Config read from (1 /etc, 2 $HOME, 3 both)		*/

	char  frontend_name[PFQL_BUF];	/* Frontend to use 					*/
	char  frontends_path[PFQL_BUF];	/* Frontend path					*/
};

// Status at runtime
struct pfql_status_t {
	short auto_wrk_tagged;		/* Automatically work on tagged messages, if any	*/
	short wrk_tagged;		/* Work on tagged messages, if any 			*/
	short ask_confirm;		/* Ask for confirmations 				*/
	short do_scan;			/* Scan messages 					*/
	short use_envelope;		/* Use envelope for from/to 				*/
	short use_colors;		/* Use colors 						*/
	short cur_queue;		/* Current queue 					*/
	short sort_field;		/* Sort field 						*/
	short sort_sense;		/* Sort ascendent/descendent 				*/
	short queue_status;		/* Tells wether the queue is being digged or not	*/
};

struct pfql_frontend_t {

	// ptr to library
	void *feptr;

	// API
	int (*pfqfe_init)(void*);
	int (*pfqfe_start)();
	const char* (*pfqfe_name)();
	const char* (*pfqfe_version)();
	void (*pfqfe_set_path)(const char*);

	char path[PFQL_BUF];
};

// pfqlib context private structure
struct pfql_context_t {
	struct msg_t *queue;
	struct be_msg_t *queue_thread;
	struct pfql_status_t pfql_status;
	struct pfql_conf_t   pfql_conf;
	
	int dig_lastqueue;
	time_t queue_last_changed;
	int NUMMSG;
	int NUMTAG;

	int thread_control;

	void *beptr;
	char* (*pfqbe_id)();
	char* (*pfqbe_version)();
	char* (*pfqbe_path)();
	int (*pfqbe_apiversion)();
	int (*pfqbe_init)();
	int (*pfqbe_setup)(struct msg_t*,struct be_msg_t*);
	int (*pfqbe_close)();
	int (*pfqbe_fill_queue)();
	int (*pfqbe_retr_headers)(const char*);
	int (*pfqbe_retr_status)(const char*);
	int (*pfqbe_retr_body)(const char*,char*,size_t);
	int (*pfqbe_message_delete)(const char*);
	int (*pfqbe_message_hold)(const char*);
	int (*pfqbe_message_release)(const char*);
	int (*pfqbe_message_requeue)(const char*);
	int (*pfqbe_set_queue)(int);
	char* (*pfqbe_queue_name)(int);
	void (*pfqbe_use_envelope)(int);
	int (*pfqbe_get_caps)();
	int (*pfqbe_queue_count)();
	struct pfb_conf_t* (*pfqbe_getconf)();
	void (*pfqbe_set_path)(const char*);

	struct pfql_frontend_t frontend;

	regex_t  *regexp;
	int search_mode;

	pthread_t       qfill_thread;
	pthread_mutex_t qfill_mutex;
};

/* -----------------
 * Library functions
 * ----------------- */

// Initializes library
int		pfql_init(struct pfql_context_t *);

// Create context
int		pfql_context_create ( struct pfql_context_t **);

// Destroy context
int		pfql_context_destroy ( struct pfql_context_t *);

// Start the backend
int		pfql_backend_start(struct pfql_context_t *);

// Start the frontend
int		pfql_frontend_start(struct pfql_context_t *);

// Stops the frontend
int		pfql_frontend_stop(struct pfql_context_t *);

// Returns library version
const char*	pfql_version();

// Returns status
struct pfql_status_t *pfql_getstatus(struct pfql_context_t *);

// Returns current configuration
struct pfql_conf_t   *pfql_getconf(struct pfql_context_t *);

/*
 * Backend functions
 */

// Returns backend API version
int	 	pfql_backend_apiversion(struct pfql_context_t *);

// Returns backend file path
const char*	pfql_backend_path(struct pfql_context_t *);

// Returns backend identifier
const char*	pfql_backend_id(struct pfql_context_t *);

// Returns backend version
const char*	pfql_backend_version(struct pfql_context_t *);

// Sets backend configuration path/file
void		pfql_backend_setconfig(struct pfql_context_t *,const char*);

// Sets backend MTA command
void		pfql_backend_setcommand(struct pfql_context_t *,const char*);

// Sets MTA version
void		pfql_backend_setversion(struct pfql_context_t *,const char*);

// Number of queues
int		pfql_num_queues(struct pfql_context_t *);

// Returns queue name
const char* 	pfql_queue_name(struct pfql_context_t *,int);

// Set queue sorting
void		pfql_queue_sort(struct pfql_context_t *);

// Sets current queue
int		pfql_set_queue(struct pfql_context_t *,int);

// The last time the queue changed
time_t		pfql_queue_last_changed(struct pfql_context_t *);

// Retreive message status
int 		pfql_retr_status(struct pfql_context_t *,const char*);

// Retreive message headers
int		pfql_retr_headers(struct pfql_context_t *,const char*);

// Retreive message body
int		pfql_retr_body(struct pfql_context_t *,const char*, void*, size_t);

// Returns the number of messages in the queue
int		pfql_num_msg(struct pfql_context_t *);

// Returns the number of tagged messages
int		pfql_num_tag(struct pfql_context_t *);

// Tags all messages
void		pfql_tag_all(struct pfql_context_t *);

// Untag all messages
void		pfql_tag_none(struct pfql_context_t *);

// Tags a single message
void		pfql_msg_tag(struct pfql_context_t *,const char*);

// Untags a single message
void		pfql_msg_untag(struct pfql_context_t *,const char*);

// Toggle tag flag on message
void		pfql_msg_toggletag(struct pfql_context_t *,const char*);

// Returns wether the message is tagged or not
int		pfql_msg_istagged(struct pfql_context_t *,const char*);

// Performs an action on the message
void		pfql_msg_action(struct pfql_context_t *,const char*,int);

// Toggle reading from envelope, if the backend supports it
void		pfql_toggle_envelope(struct pfql_context_t *);

// Search for a regexp (first occurence)
int		pfql_msg_search(struct pfql_context_t *,const char*);

// Search message forward
int		pfql_msg_searchnext(struct pfql_context_t *,const char*);

// Search message backward
int		pfql_msg_searchprev(struct pfql_context_t *,const char*);

// Search next message and tag it
void		pfql_msg_searchandtag(struct pfql_context_t *,const char*);

// Returns message at a given position in the queue
struct msg_t*	pfql_msg_at(struct pfql_context_t *,int);

// Returns a message given its ID
struct msg_t*	pfql_msg(struct pfql_context_t *,const char*);

// Store messages in a file
int		pfql_dump(struct pfql_context_t *, const char* );

// Queue status
#define		PFQL_Q_FILLING		0
#define		PFQL_Q_IDLE		1
#define		PFQL_Q_SORTING		2

// Return codes
#define		PFQL_OK			0
#define		PFQL_ERROR		-1
#define		PFQL_INVREGEXP		-1
#define		PFQL_BENOTFOUND		-2
#define		PFQL_BEWRONGAPI 	-3
#define		PFQL_BEMISSINGSYM	-4
#define		PFQL_MALLOC		-5
#define		PFQL_NOBE		-6
#define		PFQL_BEINIT		-7
#define		PFQL_MSGNOTEX		-1
#define		PFQL_NOFILE		-8

// Sort fields
#define		PFQL_SORT_UNSORTED	0
#define		PFQL_SORT_FROM		1
#define 	PFQL_SORT_TO		2
#define 	PFQL_SORT_SUBJECT	3

#define		PFQL_SORT_ASC		1
#define		PFQL_SORT_DESC		-1

// Misc
#define		PFQL_MINBEVERSION	4

#ifdef __cplusplus
}
#endif

#endif // __PFQLIB_H

