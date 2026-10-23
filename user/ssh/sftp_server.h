#pragma once
#include <m5os/types.h>
#define SFTP_SERVER_PACKET_MAX 65536u
#define SFTP_SERVER_READ_MAX 4096u
#define SFTP_SERVER_HANDLES_MAX 4u
typedef struct sftp_server sftp_server_t;
/* One task owns each session. Policy is immutable after creation. */
sftp_server_t *sftp_server_create(bool writable);
/* Consumes a prefix; returns0 while a response needs draining. Retain unconsumed input. */
size_t sftp_server_feed(sftp_server_t *server, const void *data, size_t length);
/* Drain pending wire bytes, including the4-byte SFTP packet length prefix. */
size_t sftp_server_read(sftp_server_t *server, void *data, size_t capacity);
bool sftp_server_failed(const sftp_server_t *server);
/* Closes every FD, frees the session; returns first close/sync error or0. */
int sftp_server_destroy(sftp_server_t *server);
