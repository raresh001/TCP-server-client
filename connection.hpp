#ifndef _CONNECTION_HPP
#define _CONNECTION_HPP

#include <string>
#include <queue>

#include <arpa/inet.h>

#include "epoll_info.hpp"

// first byte from every message
enum message_info {
    ID = '0',
    SUBSCRIBE = '1',
    UNSUBSCRIBE = '2',
    INFO = '3',
    EXIT = '4'
};

class connection {
public:
    enum {
        STATE_CONNECTING,
        STATE_ACTIVE,
        STATE_INVALID,
        STATE_DISCONNECTED,
        STATE_CONNECTION_BROKEN
    } state;

    std::string ID;
    const sockaddr_in addr;

    // unread received messages
    std::queue<std::string> recv_messages;

    connection(int epollfd, int connectionfd, const sockaddr_in& addr);
    ~connection();

    // read from the connection untill it would block
    void recv_message();

    // add a message to the sending queue and call send_messages()
    void push_send_message(const std::string& message);

    // send as much info as possible on the socket
    void send_messages();

    // modify epoll event parameter
    void set_monitor(int new_monitor);
private:
    const static char ETX = 0x3; // Marks the end of a message

    int epollfd;
    int connectionfd;

    int monitored_events;
    epoll_event_info<connection> epoll_info;

    std::string receiving_message;

    std::queue<std::string> sending_messages;
    int index_send_message;
};

#endif  // _CONNECTION_HPP
