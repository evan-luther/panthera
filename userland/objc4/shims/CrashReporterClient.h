/* Panthera shim: CrashReporterClient.h */
#ifndef _CRASHREPORTERCLIENT_H
#define _CRASHREPORTERCLIENT_H

#define CRSetCrashLogMessage(msg) /* nothing */
#define CRGetCrashLogMessage()    ((const char *)0)

#define CRASHREPORTER_ANNOTATIONS_VERSION 5
#define CRASHREPORTER_ANNOTATIONS_SECTION "__crash_info"

#endif /* _CRASHREPORTERCLIENT_H */
