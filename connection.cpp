#include <iostream>
#include <string.h>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/unistd.h>
#include <netinet/tcp.h>

#include "utils.h"
#include "connection.hpp"

using namespace std;

connection::connection(int epollfd, int connectionfd, const sockaddr_in& addr) 
                                                : epoll_info(this),
                                                    epollfd(epollfd),
                                                    connectionfd(connectionfd),
                                                    addr(addr),
                                                    index_send_message(0),
                                                    state(STATE_CONNECTING) {

    // set connection as non-blocking
    int connectionfd_flags = fcntl(connectionfd, F_GETFL);
    DIE(connectionfd_flags == -1 
            || fcntl(connectionfd, F_SETFL, connectionfd_flags | O_NONBLOCK) == -1, 
        "Cannot set this connection as non-blocking");

    // disable Nagle's algorithm
    int sockopt = 1;
    DIE(setsockopt(connectionfd, SOL_TCP, TCP_NODELAY, &sockopt, sizeof(sockopt)) == -1, 
        "disable of Naggle's algorithm failed");

    // put this connection in epoll
    monitored_events = EPOLLIN;
    epoll_event event;
    event.events = monitored_events;
    event.data.ptr = &epoll_info;

    DIE(epoll_ctl(epollfd, EPOLL_CTL_ADD, connectionfd, &event) == -1,
        "Adding connection to epoll failed");
}

connection::~connection() {
    DIE(epoll_ctl(epollfd, EPOLL_CTL_DEL, connectionfd, NULL) == -1,
        "Error at removing a connection");
    DIE(close(connectionfd) == -1, "Error at closing a connection socket");
}

void connection::recv_message() {
    constexpr int MAX_BUFFER_SIZE = 2048;

    if ((monitored_events & EPOLLIN) == 0) {
        // make epoll monitor message receiving as well
        set_monitor(monitored_events | EPOLLIN);
    }

    ssize_t read_size;
    char buffer[MAX_BUFFER_SIZE];

    while ((read_size = recv(connectionfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[read_size] = '\0';

        const char* iter = buffer;
        const char* etx_pos;

        // split the whole received buffer into messages
        while ((etx_pos = strchr(iter, ETX)) != nullptr) {
            receiving_message.append(iter, etx_pos - iter);
            recv_messages.push(receiving_message);
            receiving_message.clear();
            iter = etx_pos + 1;
        }

        receiving_message.append(iter, (buffer + read_size) - iter);
    }

    if (read_size == 0 || (read_size == -1 && errno != EAGAIN)) {
        state = STATE_CONNECTION_BROKEN;
    }
}

void connection::push_send_message(const string& message) {
    // set epoll to monitor writing as well
    if ((monitored_events & EPOLLOUT) == 0) {
        set_monitor(monitored_events | EPOLLOUT);
    }

    string final_message(message);
    final_message.push_back(ETX);
    sending_messages.push(final_message);

    send_messages();
}

void connection::send_messages() {
    while (!sending_messages.empty()) {
        while (index_send_message < sending_messages.front().size()) {
            ssize_t send_size = send(connectionfd,
                            sending_messages.front().data() + index_send_message,
                            sending_messages.front().size() - index_send_message,
                            0);

            if (send_size > 0) {
                index_send_message += send_size;
            } else {
                if (send_size == 0 || errno != EAGAIN) {
                    state = STATE_CONNECTION_BROKEN;
                }

                return;
            }
        }

        index_send_message = 0;
        sending_messages.pop();
    }

    // no more messages shall be sent, change epoll so that it does not
    // monitot transmitting information anymore
    set_monitor(EPOLLIN);

    if (state == STATE_INVALID)
        state = STATE_DISCONNECTED;
}

void connection::set_monitor(int new_monitor) {
    monitored_events = new_monitor;
    epoll_event event;
    event.events = monitored_events;
    event.data.ptr = &epoll_info;

    DIE(epoll_ctl(epollfd, EPOLL_CTL_MOD, connectionfd, &event) == -1,
            "Modifying connection to epoll failed");
}
