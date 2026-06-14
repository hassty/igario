#define OS_IMPLEMENTATION_WINDOWS
#include "base/os.h"
#include "base/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>

#include "os.h"

// TODO: this is not really a socket
struct os_socket {
    struct addrinfo *ainfo;
    SOCKET sock;
};

static os_socket socket_windows;

typedef struct {
    os_thread_fn fn;
    void *arg;
} os_thread_entry;

// TODO: arena alloc?
static os_thread_entry thread_entry_windows;

DWORD WINAPI os_thread_fn_windows(LPVOID arg) {
    ASSERT(arg != NULL, "invalid arg");

    os_thread_entry* entry = (os_thread_entry*)arg;
    entry->fn(entry->arg);
    return 0;
}

void os_create_thread(os_thread_fn fn, void *arg) {
	DWORD thread_id = 0;
    thread_entry_windows.fn = fn;
    thread_entry_windows.arg = arg;
	HANDLE thread_handle = CreateThread(NULL, 0, os_thread_fn_windows, &thread_entry_windows, 0, &thread_id);
    if (thread_handle == NULL) {
        //LOG_ERR("os_create_thread: error");
        exit(EXIT_FAILURE);
    }
}

os_socket *os_udp_socket(const char *hostname, const char *port) {
	WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

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

    for (socket_windows.ainfo = result; socket_windows.ainfo != NULL;
         socket_windows.ainfo = result->ai_next) {
        socket_windows.sock = socket(socket_windows.ainfo->ai_family, socket_windows.ainfo->ai_socktype,
                                   socket_windows.ainfo->ai_protocol);
        if (socket_windows.sock == -1) {
            continue;
        }

        break;
    }

    if (socket_windows.ainfo == NULL) {
        // LOG_ERR("failed to create socket");
        exit(EXIT_FAILURE);
    }
    return &socket_windows;
}

i32 os_sendto(os_socket *socket, const u8 *payload, usize payload_size) {
    return sendto(socket->sock, (const char*)payload, (i32)payload_size, 0, socket->ainfo->ai_addr,
                  (i32)socket->ainfo->ai_addrlen);
}

i32 os_recvfrom(os_socket *socket, u8 *buffer, usize buffer_size) {
    return recvfrom(socket->sock, (char*)buffer, (i32)buffer_size, 0, NULL, NULL);
}
