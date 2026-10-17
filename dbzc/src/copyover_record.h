#ifndef DBZC_COPYOVER_RECORD_H
#define DBZC_COPYOVER_RECORD_H

#include <stdio.h>

#define COPYOVER_NAME_SIZE 100
#define COPYOVER_HOST_SIZE 1024
#define COPYOVER_CLIENT_SIZE 256
#define COPYOVER_LINE_SIZE (2 * (COPYOVER_NAME_SIZE + COPYOVER_HOST_SIZE + COPYOVER_CLIENT_SIZE) + 160)

typedef struct copyover_record {
    int desc, room, lastroom, port, idle, legacy_emergency;
    char name[COPYOVER_NAME_SIZE];
    char host[COPYOVER_HOST_SIZE];
    char client[COPYOVER_CLIENT_SIZE];
} COPYOVER_RECORD;

/* Versioned, hex-encoded string fields: spaces/control bytes cannot split lines. */
int copyover_write_record(FILE *fp, int desc, int room, int lastroom,
                          int port, int idle, const char *name,
                          const char *host, const char *client);

#endif
