bin_PROGRAMS=pfqueue
pfqueue_SOURCES=config.h pfqhelp.h pfqmessage.h pfregex.h pfqlib.h pfqmessage.h pfqconfig.c pfqconfig.h pfqueue.c
pfqueue_LDADD=libpfq/libpfqueue.la

man_MANS=pfqueue.1 pfqueue.conf.5

EXTRA_DIST=pfqueue.spec.in $(man_MANS) 

SUBDIRS=libpfq backends frontends

