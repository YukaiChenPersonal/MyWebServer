#include "socket_wrap.h"



int c_socket(int domain, int type, int protocol){
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if(lfd == -1){
        EMlog(LOGLEVEL_ERROR, "socket error, check arguments...");
    }
    assert(lfd != -1);
    return lfd;
}

void c_bind(int fd, int port){
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int ret = bind(fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if(ret == -1){
        EMlog(LOGLEVEL_ERROR, "bind error, check arguments...");
    }
    assert( ret != -1);
    return;
}

void c_listen(int fd, int backlog){
    int ret = listen(fd, backlog);
    if(ret == -1){
        EMlog(LOGLEVEL_ERROR, "listen error, check arguments...");
    }
    assert( ret != -1);
    return;
}

int c_accept(int fd){
    struct sockaddr_in clit_addr;
    socklen_t clit_addrlen = sizeof(clit_addr);

    int cfd = accept(fd, (struct sockaddr*)&clit_addr, &clit_addrlen);
    if(cfd == -1){
        EMlog(LOGLEVEL_ERROR, "accept error, check arguments...");
    }
    assert(cfd != -1);
    return cfd;
}

void set_port_reuse(int fd){
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
}

void set_socket_nonblock(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

void c_socketpair(int sv[2]){
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sv);
    if(ret == -1){
        EMlog(LOGLEVEL_ERROR, "socketpair error, check argument...");
    }
    assert( ret!= -1);
    return;
}
    