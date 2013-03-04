
#ifndef PFQ_SERVICE_H
#define PFQ_SERVICE_H

#include "../pfqmessage.h"

int freadl ( FILE *fp, char* buf, int buflen );
int flookfor ( FILE *fp, char* buf, int bufsize, char* w );

int fs_should_dig ( struct dirent* ent, const char* path );
int fs_should_add ( struct dirent* ent, const char* path );

#define PRIV_BUF_SIZE 200

#endif

