#ifndef _OS_H_

#include "base/base.h"

typedef void (*os_thread_fn)(void*);
void os_create_thread(os_thread_fn fn, void *arg);

typedef struct os_socket os_socket;
os_socket *os_udp_socket(const char *hostname, const char *port);
i32 os_sendto(os_socket *socket, const u8 *payload, usize payload_size);
i32 os_recvfrom(os_socket *socket, u8 *buffer, usize buffer_size);

#endif /* _OS_H_ */

