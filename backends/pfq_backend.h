
/*
 * pfqueue backend
 */

#ifndef PFQ_BACKEND_H
#define PFQ_BACKEND_H

#define LOGLEVEL LOG_USER | LOG_ERR

#define BECAPS_MSG_DEL		1	// Can delete messages
#define BECAPS_MSG_HOLD		2	// Can hold (freeze) messages
#define BECAPS_MSG_REQUEUE	4	// Can requeue messages
#define BECAPS_MSG_ENVELOPE	8	// Can extract from/to from envelope
#define BECAPS_MIXED_QUEUE	16	// Backend is single-queue, with status somewhere in some files
#define BECAPS_MSG_LOG		32	// Each message has its own log

#define BUF_SIZE		250

#ifndef FALSE
#define FALSE			0
#endif

#include "../pfqmessage.h"

#define PFQ_API_VERSION		4

#define PFBE_OK			0
#define PFBE_ERROR		-3
#define PFBE_UNUSABLE		1
#define PFBE_MSGNOTEX		-1
#define PFBE_MSGCACHED		-2

#define PFBE_SERROR	"*Error*"
#define PFBE_SNOTFOUND	"*Not found*"

struct pfb_conf_t {
	int	max_char;		/* char buffer max length */
	char    backend_path[200];	/* path to backend */
	char	command_path[200];	/* path to executables */
	char	config_path[200];	/* path to config */
	int	msg_max;		/* max num of messages */
	int	scan_limit;		/* max secs for a single scan */
	char	version[200];		/* version of mta to use */
	char	host[200];		/* remote host (only for socket backend) */
	int	port;			/* remote port (only for socket backend) */
};

int pfb_caps;
int pfb_using_envelope;

// Returns backend API version
int pfb_apiversion();

// Returns backend path
const char *pfb_path();

// Returns backend id string
const char *pfb_id();

// Returns backend version
const char *pfb_version();

// Returns conf structure
struct pfb_conf_t *pfb_getconf();

// Sets backend path
void pfb_set_path(const char*);

// Init backend
int pfb_init();

// Setup backend
int pfb_setup ( struct msg_t*, struct be_msg_t* );

// Closes backend
int pfb_close();

// Fill message queue (called from pthread)
int pfb_fill_queue ();

// Retreive from/to/subject fields
int pfb_retr_headers ( const char* );

// Retreive message status
int pfb_retr_status ( const char* );

// Retreive message body
int pfb_retr_body ( const char*, char*, size_t );

// Delete a message
int pfb_message_delete ( const char* );

// Hold a message
int pfb_message_hold ( const char* );
// 
// Release a message
int pfb_message_release ( const char* );

// Requeue a message
int pfb_message_requeue ( const char* );

// Change current queue
int pfb_set_queue ( int );

// Set/unset use of envelope for from/to retreival
void pfb_use_envelope ( int );

// Get backend caps
int pfb_get_caps();

// Returns the queue name
char* pfb_queue_name( int );

// Returns the number of the queues
int pfb_queue_count();

// Ptr to main queue
struct be_msg_t *my_queue;

// Ptr to backend queue
struct msg_t *ext_queue;

// Sets the number of seconds the queue scanning should not exceed
int dig_limit;

// Time digging was last started
int dig_start;

// Max number of messages to handle
int msg_max;

#endif
