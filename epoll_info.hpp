#ifndef _EPOLL_INFO_H
#define _EPOLL_INFO_H

template <typename T>
struct epoll_event_info {
    enum {
        FD,
        PTR
    } info_type;

    union {
        int fd;
        T* data;
    } info;

    epoll_event_info(int fd) {
        info_type = FD;
        info.fd = fd;
    }

    epoll_event_info(T* data) {
        info_type = PTR;
        info.data = data;
    }
};

#endif  // _EPOLL_INFO_H