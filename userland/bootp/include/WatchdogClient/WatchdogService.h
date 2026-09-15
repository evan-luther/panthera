#ifndef PANTHERA_WATCHDOGSERVICE_H
#define PANTHERA_WATCHDOGSERVICE_H

#include <dispatch/dispatch.h>

/*
 * Apple IPConfiguration optionally registers its dispatch queue with
 * WatchdogClient. Panthera does not stage watchdogd yet, and the source already
 * weak-links wd_endpoint_add_queue, so this header intentionally only provides
 * the declaration needed to preserve that runtime NULL check.
 */
void wd_endpoint_add_queue(dispatch_queue_t queue_to_monitor)
	__attribute__((weak_import));

#endif /* PANTHERA_WATCHDOGSERVICE_H */
