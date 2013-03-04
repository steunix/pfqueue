
/*
 * pfqueue frontend
 */

#ifndef PFQ_FRONTEND_H
#define PFQ_FRONTEND_H

#define LOGLEVEL LOG_USER | LOG_ERR

// Initialize the frontend
int pff_init(void*);

// Start the frontend
int pff_start();

const char* pff_name();
const char* pff_version();

struct pfql_context_t *pfql_ctx;

#endif
