#define OS_IMPLEMENTATION_LINUX
#include "base/os.h"
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "os.h"

struct os_socket {
    struct addrinfo *ainfo;
    i32 sock;
};

static os_socket socket_linux;

typedef struct {
    os_thread_fn fn;
    void *arg;
} os_thread_entry;

static os_thread_entry thread_entry_linux;

static void *os_thread_fn_linux(void *arg) {
    ASSERT(arg != NULL, "invalid arg");

    os_thread_entry *entry = (os_thread_entry*)arg;
    entry->fn(entry->arg);
    return NULL;
}

void os_create_thread(os_thread_fn fn, void *arg) {
    pthread_t t = 0;
    thread_entry_linux.fn = fn;
    thread_entry_linux.arg = arg;
    i32 err = pthread_create(&t, NULL, os_thread_fn_linux, &thread_entry_linux);
    if (err != 0) {
        // LOG_ERR("pthread_create: %d", err);
        exit(EXIT_FAILURE);
    }
}

os_socket *os_udp_socket(const char *hostname, const char *port) {
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
    };

    struct addrinfo *result = NULL;
    int err = getaddrinfo(hostname, port, &hints, &result);
    if (err != 0) {
        // LOG_ERR("getaddrinfo: %s", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    for (socket_linux.ainfo = result; socket_linux.ainfo != NULL;
         socket_linux.ainfo = result->ai_next) {
        socket_linux.sock = socket(socket_linux.ainfo->ai_family, socket_linux.ainfo->ai_socktype,
                                   socket_linux.ainfo->ai_protocol);
        if (socket_linux.sock == -1) {
            continue;
        }

        break;
    }

    if (socket_linux.ainfo == NULL) {
        // LOG_ERR("failed to create socket");
        exit(EXIT_FAILURE);
    }
    return &socket_linux;
}

i32 os_sendto(os_socket *socket, const u8 *payload, usize payload_size) {
    return sendto(socket->sock, payload, payload_size, 0, socket->ainfo->ai_addr,
                  socket->ainfo->ai_addrlen);
}

i32 os_recvfrom(os_socket *socket, u8 *buffer, usize buffer_size) {
    return recvfrom(socket->sock, buffer, buffer_size, 0, NULL, NULL);
}
