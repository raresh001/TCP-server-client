#include <iostream>
#include <string>
#include <set>
#include <string.h>
#include <stdio.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/unistd.h>

#include "utils.h"
#include "connection.hpp"

using namespace std;

class client {
public:
    connection conn;
    bool finished;

    client(int epollfd, int socketfd, const sockaddr_in& addr)
        : conn(epollfd, socketfd, addr), finished(false) {}

    // add the topic to the pending list of subscribes
    void subscribe(const string& topic) { pending_subscribed.insert(topic); }

    // add the topic to the pending list of unsubscribes
    void unsubscribe(const string& topic) { pending_unsubscribed.insert(topic); }

    // manage the event; returns if the connection is still functional
    bool manage_connection(epoll_event& event);
private:
    set<string> pending_subscribed;
    set<string> pending_unsubscribed;
    set<string> subscribed;
};

bool client::manage_connection(epoll_event& event) {
    if (event.events | EPOLLIN) {
        conn.recv_message();

        while (!conn.recv_messages.empty()) {
            string message = conn.recv_messages.front();
            conn.recv_messages.pop();

            switch (message[0]) {
                case ID:
                    if (strcmp(message.data() + 1, "OK")) {
                        // ID is already used; return
                        return false;
                    }
                    break;
                
                case SUBSCRIBE:
                    {
                        string response = string(message.data() + 2);
                        auto pending_iterator = pending_subscribed.find(response);

                        if (pending_iterator == pending_subscribed.end()) {
                            // simply ignore it
                            break;
                        }

                        pending_subscribed.erase(pending_iterator);

                        if (message[1] == '0') {
                            cout << "Subscribed to topic " << response << endl;
                            subscribed.insert(response);
                        }
                    }

                    break;

                case UNSUBSCRIBE:
                    {
                        string response = string(message.data() + 2);
                        auto pending_iterator = pending_unsubscribed.find(response);

                        if (pending_iterator == pending_unsubscribed.end()) {
                            // simply ignore it
                            break;
                        }

                        pending_unsubscribed.erase(pending_iterator);

                        if (message[1] == '0') {
                            cout << "Unubscribed from topic " << response << endl;
                            subscribed.erase(response);
                        }
                    }

                    break;

                case INFO:
                    cout << message.data() + 1 << endl;

                break;

                case EXIT:
                    // connection is closing nicely
                    return false;

                default:
                    // Wrong message type
                    break;
            }
        }

        if (conn.state == connection::STATE_CONNECTION_BROKEN) {
            return false;
        }
    }

    if (event.events | EPOLLOUT) {
        conn.send_messages();
        if (finished) { // when the client is shuting down
            return false;
        }

        if (conn.state == connection::STATE_CONNECTION_BROKEN) {
            // Connection to the server closed unexpectedly
            return false;
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        // Wrong call of client: it should be:
        // ./subscriber <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n";
        return 1;
    }

    // unbuffer STDOUT
    DIE(setvbuf(stdout, NULL, _IONBF, BUFSIZ), "Unbuffering STDOUT failed");

    int epollfd = epoll_create1(0);
    DIE(epollfd == -1, "Cannot create epoll");

    // add STDIN to epoll
    epoll_event_info<connection> stdin_epoll_info(STDIN_FILENO);
    epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = &stdin_epoll_info;
    DIE(epoll_ctl(epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1,
        "Adding STDIN to epoll failed");

    // Create socket and connect it to the server
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(socketfd == -1, "Cannot create socket.");

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) atoi(argv[3]));
    inet_aton(argv[2], &addr.sin_addr);

    DIE(connect(socketfd, (sockaddr*) &addr, sizeof(addr)) == -1, "connect failed");

    client c(epollfd, socketfd, addr);

    // send the ID
    c.conn.set_monitor(EPOLLOUT);
    c.conn.push_send_message(string((char)ID + string(argv[1])));
    if (c.conn.state == connection::STATE_CONNECTION_BROKEN) {
        // Connection closed unexpectedly
        return 0;
    }

    while (true) {
        epoll_event event;
        DIE(epoll_wait(epollfd, &event, 1, -1) == -1, "Waiting failed");

        epoll_event_info<connection>* info = (epoll_event_info<connection> *)event.data.ptr;

        switch (info->info_type) {
            case epoll_event_info<connection>::FD: // STDIN
                {
                    string command;
                    getline(cin, command, '\n');

                    if (strncmp(command.data(), "subscribe", sizeof("subscribe") - 1) == 0) {
                        // send a request for subscribe
                        string message(string("") + (char) SUBSCRIBE);
                        string topic(command.data() + sizeof("subscribe ") - 1);

                        c.subscribe(topic);
                        message += topic;
                        c.conn.push_send_message(message);

                        if (c.conn.state == connection::STATE_CONNECTION_BROKEN) {
                            // Connection closed unexpectedly
                            return 0;
                        }
                    } else if (strncmp(command.data(), "unsubscribe", sizeof("unsubscribe") - 1) == 0) {
                        // send a request for unsubscribe
                        string message(string("") + (char) UNSUBSCRIBE);
                        string topic(command.data() + sizeof("unsubscribe ") - 1);

                        c.unsubscribe(topic);
                        message += topic;
                        c.conn.push_send_message(message);

                        if (c.conn.state == connection::STATE_CONNECTION_BROKEN) {
                            // Connection closed unexpectedly
                            return 0;
                        }
                    } else if (strncmp(command.data(), "exit", sizeof("exit")) == 0) {
                        // send EXIT message
                        c.finished = true;
                        c.conn.set_monitor(EPOLLOUT);
                        c.conn.push_send_message(string(string("") + (char) EXIT));
                        if (c.conn.state == connection::STATE_CONNECTION_BROKEN
                            || c.conn.state == connection::STATE_DISCONNECTED) {
                            return 0;
                        }
                    }
                }

                break;
            case epoll_event_info<connection>::PTR:
                if (!c.manage_connection(event)) {
                    return 0;
                }
                break;
            default:
                DIE(true, "Wrong type of connection here");
        }


    }

    return 0;
}
