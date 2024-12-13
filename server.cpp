#include <string>
#include <iostream>
#include <string.h>
#include <sstream>
#include <iomanip>

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include "utils.h"
#include "server.hpp"

using namespace std;

int server::create_binded_listenfd(int type, uint16_t port) {
    int listenfd = socket(AF_INET, type, 0);
    int sockopt;

    // make this socket reuse the address (so that both TCP and UDP sockets can
    // bind to the same socket)
    DIE(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt)) == -1,
            "Cannot make the socket reuse the given address");
    DIE(listenfd == -1, "Cannot create TCP listener");

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY; // get server's IP address

    // set the listening socket as non-blocking
    int socket_flags = fcntl(listenfd, F_GETFL);
    DIE(socket_flags == -1
            || fcntl(listenfd, F_SETFL, socket_flags | O_NONBLOCK) < 0,
        "Cannot set listening fd as non-blocking");

    DIE(bind(listenfd, (const sockaddr *) &addr, sizeof(addr)) < 0,
            "Cannot realise bind\n");

    return listenfd;
}

server::server(uint16_t port) : stdin_epoll_info(STDIN_FILENO), closed(false) {
    // create epoll
    epollfd = epoll_create1(0);
    DIE(epollfd == -1, "Cannot realise epoll");

    // add STDIN to epoll
    epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = &stdin_epoll_info;
    DIE(epoll_ctl(epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1,
        "Adding STDIN to epoll failed");

    // create TCP listener
    tcp_listen_fd = create_binded_listenfd(SOCK_STREAM, port);
    DIE(listen(tcp_listen_fd, 10) == -1, "listen failed");

    tcp_listener_epoll_info = new epoll_event_info<connection>(tcp_listen_fd);
    event.data.ptr = tcp_listener_epoll_info;

    DIE(epoll_ctl(epollfd, EPOLL_CTL_ADD, tcp_listen_fd, &event) == -1,
        "Adding TCP listenfd to epoll failed");

    // create UDP listener
    udp_listen_fd = create_binded_listenfd(SOCK_DGRAM, port);
    udp_listener_epoll_info = new epoll_event_info<connection>(udp_listen_fd);

    event.data.ptr = udp_listener_epoll_info;
    DIE(epoll_ctl(epollfd, EPOLL_CTL_ADD, udp_listen_fd, &event) == -1,
        "Adding UDP listenfd to epoll failed");
}

server::~server() {
    // close all connections
    for (auto& pair : clients) {
        delete pair.second;
    }

    for (auto iter : refused_clients) {
        delete iter;
    }

    delete tcp_listener_epoll_info;
    delete udp_listener_epoll_info;

    // close TCP and UDP listeners
    DIE(close(tcp_listen_fd) == -1 || close(udp_listen_fd) == -1,
        "Cannot close TCP\\UDP listening file descriptor");

    // close epoll
    DIE(close(epollfd) == -1, "Cannot close epolfd");
}

void server::run() {
    while (true) {
        epoll_event event;
        DIE(epoll_wait(epollfd, &event, 1, -1) == -1, "Waiting failed");

        epoll_event_info<connection>* info = (epoll_event_info<connection> *)event.data.ptr;

        switch (info->info_type) {
            case epoll_event_info<connection>::FD:
                if (info->info.fd == STDIN_FILENO) {
                    // check if exit is required
                    char exit_message[sizeof("exit")];
                    cin.getline(exit_message, sizeof(exit_message));

                    if (strcmp(exit_message, "exit") == 0)
                        if (shutdown())
                            return;
                } else if (info->info.fd == tcp_listen_fd)
                    add_clients();
                else if (info->info.fd == udp_listen_fd)
                    manage_UDP_message();
                else
                    DIE(true, "There shouldn't be any waiting fd's \
                        in epoll other than TCP and UDP listeners.");
                break;
            case epoll_event_info<connection>::PTR:
                if (!manage_connection(info->info.data, event)) {
                    remove_connection(info->info.data);
                }
                break;
            default:
                DIE(true, "Wrong type of connection here");
        }

        if (closed && clients.empty() && refused_clients.empty()) {
            return;
        }
    }
}

bool server::add_client(connection* conn, const string& ID) {
    // right now, connection should only send the validation;
    // it shouldn't receive data
    conn->set_monitor(EPOLLOUT);

    if (clients.find(ID) == clients.end()) {
        conn->ID = ID;

        cout << "New client "
            << ID
            << " connected from "
            << inet_ntoa(conn->addr.sin_addr)
            << ":"
            << ntohs(conn->addr.sin_port)
            << "."
            << endl;

        conn->state = connection::STATE_ACTIVE;
        clients[conn->ID] = conn;
        conn->push_send_message(string((char)message_info::ID + string("OK")));
        if (conn->state == connection::STATE_CONNECTION_BROKEN) {
            // Connection closed unexpectedly
            remove_connection(conn);
            return false;
        }
    } else {
        cout << "Client " << ID << " already connected." << endl;
        refused_clients.push_back(conn);

        conn->set_monitor(EPOLLOUT);
        conn->push_send_message(string((char)message_info::ID + string("NO")));
        if (conn->state == connection::STATE_DISCONNECTED
            || conn->state == connection::STATE_CONNECTION_BROKEN) {
            remove_connection(conn);
            return false;
        }
    }

    return true;
}

void server::add_clients() {
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int connectionfd;

    while ((connectionfd = accept(tcp_listen_fd, (sockaddr *) &addr, &len)) != -1) {
        // create new connection
        connection* conn = new connection(epollfd, connectionfd, addr);

        // read the ID of connection
        manage_receive(conn);
    }

    // accept should have returned -1 if and only if EAGAIN had been set
    DIE(errno != EAGAIN, "accept failed");
}

void server::manage_UDP_message() {
    char buff[MAX_UDP_PACKAGE_SIZE];

    while (true) {
        ssize_t read_size = recvfrom(udp_listen_fd,
                                buff,
                                MAX_UDP_PACKAGE_SIZE,
                                0,
                                nullptr,
                                nullptr);

        if (read_size <= 0) {
            return;
        }

        // parse the message, in order to see if it has finished
        string message(buff, read_size);

        if (message.size() < 51) {
            // ignore incompatible packages
            continue;
        }

        string payload_message;

        switch (message[50]) {
            case 0: // INT
                {
                    if (message.size() < 56) {
                        // the message is not complete; drop it
                        continue;
                    }

                    if (message[51] > 1) {
                        // message is corrupted; drop it
                        continue;
                    }

                    uint32_t* int_p = (uint32_t*)(message.data() + 52);
                    uint32_t int_message = htonl(*int_p);
                    
                    payload_message.append(" - INT - ");
                    if (message[51] && int_message != 0) {
                        payload_message.append("-");
                    }
                    payload_message.append(to_string(int_message));
                }

                break;
        
            case 1: // SHORT REAL
                {
                    if (message.size() < 53) {
                        // the message is not complete; drop it
                        continue;
                    }

                    uint16_t* int_p = (uint16_t*)(message.data() + 51);
                    uint16_t int_message = htons(*int_p);
                    
                    payload_message.append(" - SHORT_REAL - ");

                    stringstream ss;
                    ss << fixed << setprecision(2) << (float)int_message / 100;
                    string result;
                    ss >> result;

                    payload_message.append(result);
                }

                break;

            case 2: // FLOAT
                {
                    if (message.size() < 57) {
                        // the message is not complete; drop it
                        continue;
                    }

                    if (message[51] > 1) {
                        // message is corrupted; drop it
                        continue;
                    }

                    uint32_t* int_p = (uint32_t*)(message.data() + 52);
                    uint32_t module = htonl(*int_p);

                    uint8_t exp = *(uint8_t*)(message.data() + 56);

                    payload_message.append(" - FLOAT - ");
                    if (message[51]) {
                        payload_message.append("-");
                    }

                    double result = module;
                    while (exp--) {
                        result /= 10;
                    }

                    payload_message.append(to_string(result));
                }

                break;

            case 3: // STRING
                payload_message.append(" - STRING - ");
                for (int iter = 51;
                        iter < MAX_UDP_PACKAGE_SIZE && message[iter];
                        iter++) {

                    payload_message += message[iter];
                }
                break;

            default:
                // no valid data type; drop the package
                continue;
        }

        // parse the topic
        int i;
        for (i = 0; i < 50; i++) {
            if (message[i] == '\0') {
                break;
            }
        }
        string topic(message, 0, i);

        set<string>* IDs = topics.get_subscribers(topic.data());
        // send this message to all subscribers
        for (auto& ID : *IDs) {
            auto conn = clients.find(ID);
            if (conn != clients.end()) {
                conn->second->push_send_message(string((char)INFO + topic + payload_message));
                if (conn->second->state == connection::STATE_CONNECTION_BROKEN) {
                    // Connection closed unexpectedly
                    remove_connection(conn->second);
                }
            }
        }
        delete IDs;
    }
}

bool server::manage_client_request(connection* conn, const string& request) {
    if (conn->state == connection::STATE_CONNECTING) {
        if (request[0] == ID) {
            return add_client(conn, string(request.data() + 1));
        } else {
            // Client did not send its ID as a first message; close this connection
            remove_connection(conn);
            return false;
        }
    }
    
    switch (request[0]) {
        case ID:
            // Connection sent ID more than once; ignore this
            break;
        case SUBSCRIBE: // subscribe
            topics.subscribe(conn->ID, request.data() + 1);
            conn->push_send_message(string((char)SUBSCRIBE + string("0") + string(request.data() + 1)));
            if (conn->state == connection::STATE_CONNECTION_BROKEN) {
                // Connection closed unexpectedly
                remove_connection(conn);
                return false;
            }
            break;
        case UNSUBSCRIBE: // unsubscribe
            topics.unsubscribe(conn->ID, request.data() + 1);
            conn->push_send_message(string((char)UNSUBSCRIBE + string("0") + (request.data() + 1)));
            if (conn->state == connection::STATE_CONNECTION_BROKEN) {
                // Connection closed unexpectedly
                remove_connection(conn);
                return false;
            }
            break;
        case EXIT:
            return false;
            break;
        default:
            // Wrong request type
            break;
    }

    return true;
}

bool server::manage_receive(connection* conn) {
    conn->recv_message();

    if (conn->state == connection::STATE_CONNECTION_BROKEN) {
        // Connection closed unexpectedly
        return false;
    }

    while (!conn->recv_messages.empty()) {
        string request = conn->recv_messages.front();
        conn->recv_messages.pop();

        if (manage_client_request(conn, request) == false) {
            return false;
        }
    }

    return true;
}

bool server::manage_send(connection* conn) {
    conn->send_messages();

    switch (conn->state) {
        case connection::STATE_INVALID:
            // rejection response has been sent; close this
            remove_connection(conn);
            return false;
            break;

        case connection::STATE_CONNECTION_BROKEN:
            // Connection closed unexpectedly
            remove_connection(conn);
            return false;
            break;

        default:
            break;
    }

    return true;
}

void server::remove_connection(connection* conn) {
    if (conn->ID.empty()) {
        refused_clients.remove(conn);
    } else {
        cout << "Client " << conn->ID << " disconnected." << endl;
        clients.erase(conn->ID);
    }

    delete conn;
}

bool server::shutdown() {
    for (auto conn = clients.begin(); conn != clients.end();) {
        conn->second->state = connection::STATE_INVALID;
        conn->second->set_monitor(EPOLLOUT);
        conn->second->push_send_message(string(string("") + (char)message_info::EXIT));
        if (conn->second->state == connection::STATE_CONNECTION_BROKEN
            || conn->second->state == connection::STATE_DISCONNECTED) {

            connection* c = conn->second;
            conn++;
            remove_connection(c);
        } else {
            conn++;
        }
    }

    return clients.empty() && refused_clients.empty();
}
