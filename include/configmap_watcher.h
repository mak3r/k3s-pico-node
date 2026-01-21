#ifndef CONFIGMAP_WATCHER_H
#define CONFIGMAP_WATCHER_H

/**
 * ConfigMap Watcher
 *
 * Polls the k3s API for ConfigMap changes
 * and triggers memory updates when changes are detected
 */

/**
 * Initialize the ConfigMap watcher
 * Returns 0 on success, -1 on error
 */
int configmap_watcher_init(void);

/**
 * Poll for ConfigMap updates
 * Fetches the specified ConfigMap and processes changes
 * Should be called every CONFIGMAP_POLL_INTERVAL_MS
 * Returns 0 on success, -1 on error
 */
int configmap_watcher_poll(void);

/**
 * Force an immediate ConfigMap check
 * Returns 0 on success, -1 on error
 */
int configmap_watcher_check_now(void);

#endif // CONFIGMAP_WATCHER_H
