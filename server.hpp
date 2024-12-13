#ifndef _SERVER_TCP_UDP_HPP
#define _SERVER_TCP_UDP_HPP

#include <list>
#include <map>
#include <string>
#include <iostream>

#include <sys/epoll.h>

#include "connection.hpp"
#include "topics.hpp"

class server {
public:
    server(uint16_t port);
    ~server();

    void run();
private:
    static constexpr int MAX_UDP_PACKAGE_SIZE = 50 + 1 + 1500;

    bool closed;
    int epollfd;
    int tcp_listen_fd;
    int udp_listen_fd;

    epoll_event_info<connection> stdin_epoll_info;
    epoll_event_info<connection>* tcp_listener_epoll_info;
    epoll_event_info<connection>* udp_listener_epoll_info;

    // store them as well to close connections when the server shuts down
    std::list<connection*> refused_clients;

    std::map<std::string, connection*> clients;

    topics_tree topics;

    // assign the given ID to a connection that is not active yet
    // if the ID has already been used, the connection is refused
    // and a rejection is sent
    bool add_client(connection* conn, const std::string& ID);

    // add as many clients as possible from the TCP listening port
    void add_clients();

    void remove_connection(connection* conn);

    // read as many UDP messages as possible, and send the information given
    // to all the subscribers from the sent topic
    void manage_UDP_message();

    // receive as many messages as possible and manage their content;
    // returns if the connection is still valid; otherwise it should be removed
    bool manage_receive(connection* conn);

    // send as many messages as possible
    // returns if the connection is still valid; otherwise it will be removed here
    bool manage_send(connection* conn);

    // manage the event from epoll
    bool manage_connection(connection* conn, const epoll_event& ev) {
        if (ev.events & EPOLLIN) {
            if (manage_receive(conn) == false) {
                return false;
            }
        }

        if (ev.events & EPOLLOUT) {
            return manage_send(conn);
        }

        return true;
    }

    bool manage_client_request(connection* conn, const std::string& request);

    // starts shutdown; marks all connection as invalid and sends the EXIT message
    bool shutdown();

    static int create_binded_listenfd(int type, uint16_t port);
};

#endif  // _SERVER_TCP_UDP_HPP
