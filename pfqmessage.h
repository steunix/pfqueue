
/*
 * Message structure
 */

#ifndef __PFQMESSAGE_H
#define __PFQMESSAGE_H

#define MSG_HOLD 	0
#define MSG_DELETE  	1
#define MSG_RELEASE  	2
#define MSG_REQUEUE	3

#define Q_DEFERRED 	0
#define Q_HOLD     	1
#define Q_INCOMING 	2
#define Q_ACTIVE   	3
#define Q_CORRUPT	4

#define MSG_MOVED 	"Message moved"

// Message structure for frontend
struct msg_t {
	char  id   [20];
	char  from [100];
	char  to   [100];
	char  subj [100];
	char  path [200];
	char  stat [200];
	short hcached;	// Header cached
	short scached;	// Status cached
	short tagged;
};

// Message queue for backend
struct be_msg_t {
	char  id   [20];
	char  path [200];
	short changed;
};

#endif
