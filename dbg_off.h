#ifndef DBG_OFF_H
#define DBG_OFF_H

/* ================================================================
 * Debugging disabled
 * ================================================================ */

#define DBG_MSG(...)       ((void)0)
#define DBG_LOG(code)      code
#define DBG_LOG_OPEN(code) code
#define DBG_TRACK(...)     ((void)0)

#endif
