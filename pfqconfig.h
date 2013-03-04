
/*
 * Configuration functions for pfqueue
 */

#ifndef __PFQLIBCONFIG_H
#define __PFQLIBCONFIG_H

#include "pfqlib.h"

int pfq_read_file ( struct pfql_context_t*, const char* );
int pfq_read_config ( struct pfql_context_t* );

#endif
