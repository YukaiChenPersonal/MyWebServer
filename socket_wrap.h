#ifndef SOCKET_WRAP_H
#define SOCKET_WRAP_H

#include <sys/socket.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h> 
#include "log.h"


int c_socket(int domain, int type, int protocol);

void c_bind(int fd, int port);

void c_listen(int fd, int backlog);

int c_accept(int fd);

void set_port_reuse(int fd);

void set_socket_nonblock(int fd);

void c_socketpair(int sv[2]);


#endif