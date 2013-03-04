
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../config.h"

#include "pfq_service.h"
#include "../pfqmessage.h"

struct stat foostat;

int fs_should_dig ( struct dirent* ent, const char* path ) {
	if ( ent->d_name[0] == '.' )
		return 0;
#ifdef HAVE_DIRENT_DTYPE
	// Nice fs
	if ( ent->d_type==DT_DIR || ent->d_type==DT_LNK )
		return 1;
	// Unpolite fs
	if ( ent->d_type==DT_UNKNOWN && strlen(path) ) {
#endif
		stat ( path, &foostat );
		if ( S_ISDIR(foostat.st_mode) || S_ISLNK(foostat.st_mode) )
			return 1;
#ifdef HAVE_DIRENT_DTYPE
	}
#endif
	return 0;
}

int fs_should_add ( struct dirent* ent, const char* path ) {
	if ( ent->d_name[0]=='.' )
		return 0;
#ifdef HAVE_DIRENT_DTYPE
	// Nice fs
	if ( ent->d_type==DT_REG )
		return 1;
	// Unpolite fs
	if ( ent->d_type==DT_UNKNOWN && strlen(path) ) {
#endif
		stat ( path, &foostat );
		if ( S_ISREG(foostat.st_mode) )
			return 1;
#ifdef HAVE_DIRENT_DTYPE
	}
#endif
	return 0;
}

int freadl ( FILE *fp, char* buf, int buflen ) {
	int i,p;
	int fn;
	
	if ( !fp )
		return 0;

	p = (fgets ( buf, buflen, fp )!=NULL);
	if ( p ) {
		i = strlen ( buf );
		if ( *(buf+i-1)=='\n' )
			*(buf+i-1)='\0';
	}
	return ( p>0 );
}

int flookfor ( FILE *fp, char* buf, int bufsize, char* w ) {
	int b;
	int  wlen;
	char b2[PRIV_BUF_SIZE];

	wlen = strlen ( w );
	b = 0;
	
	while ( !b && freadl ( fp, buf, bufsize ) )
		b = !strncmp ( buf, w, wlen );
	if ( b ) {
		memcpy ( b2, buf, bufsize );
		memcpy ( buf, b2+wlen, bufsize-wlen );
		*(buf+bufsize-1) = '\0';
	}
	return ( b );
}

