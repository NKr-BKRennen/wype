/*
 *  api_server.h: Embedded HTTP/JSON API server for dashboard integration.
 *
 *  Provides a REST API so that a central wype-dashboard instance can
 *  query the live wipe status of every disk on this node.
 *
 *  Requires libmicrohttpd and cJSON.  When those libraries are not
 *  available at build time the public functions compile as harmless
 *  no-ops (see api_server.c).
 */

#ifndef API_SERVER_H_
#define API_SERVER_H_

#include "context.h"

/**
 * Start the embedded HTTP API server.
 *
 * All pointer arguments must remain valid until wype_api_server_stop()
 * is called.  The contexts_ptr is a pointer-to-pointer so that a
 * rescan (which may realloc the array) is automatically picked up.
 *
 * @param contexts_ptr  Address of the c1 variable (wype_context_t**)
 * @param count_ptr     Address of wype_enumerated
 * @param misc          Pointer to the misc/aggregate thread data
 * @param port          TCP port (0 → default 5000)
 * @param password      API password; empty/NULL → server disabled
 * @return 0 on success, -1 on failure or when disabled
 */
int wype_api_server_start( wype_context_t*** contexts_ptr,
                            int* count_ptr,
                            wype_misc_thread_data_t* misc,
                            int port,
                            const char* password );

/**
 * Stop the API server and release resources.
 */
void wype_api_server_stop( void );

/**
 * Check whether the API server is currently running.
 * @return 1 if running, 0 otherwise
 */
int wype_api_server_is_running( void );

#endif /* API_SERVER_H_ */
