#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <error.h>
#include <fcntl.h>      
#include <sys/epoll.h>
#include <signal.h>
#include <assert.h>
#include "locker.h"
#include "thread_pool.h"
#include "http_conn.h"
#include "time_check.h"
#include "log.h"
#include "socket_wrap.h"
#include <string>

#define MAX_FD 65535 // Max file descriptor number
#define MAX_EVENT 10000

static int pipefd[2];
static sort_timer_list timer_list;

//add signal capture
void addsignal(int signal, void(handler)(int)){
    struct sigaction sa;
    bzero(&sa, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);
    sigaction(signal, &sa, NULL);
}

void sig_handler(int signal){
    int save_errno = errno;
    int msg = signal;
    send(pipefd[1], (char*)&msg,1,0);
    errno = save_errno;
}
//add file descriptor to epoll event tree
extern void addfd(int epollfd, int fd, bool one_shot, bool ET);
//remove fd from epoll event tree
extern void removefd(int epollfd, int fd);
//modify a fd
extern void modfd(int epollfd, int fd, int ev);
//set a fd to non-block
extern void set_socket_nonblock(int fd);

extern int c_socket(int domain, int type, int protocol);

extern void c_bind(int fd, const struct sockaddr * addr, socklen_t addrlen);

extern void c_listen(int fd, int backlog);

extern int c_accept(int fd);

extern void set_port_reuse(int fd);

extern void c_socketpair(int sv[2]);



int main(int argc, char* argv[]){

    if(argc <3 || (atoi(argv[2])!=0 && atoi(argv[2])!=1) ){
        EMlog(LOGLEVEL_ERROR, "Input format should be:./server.out %s port number 0/1\n");
        return 1;
    }
    int port = atoi(argv[1]);
    EMlog(LOGLEVEL_INFO, "port number: %d\n", port);

    bool is_et = (atoi(argv[2])==1)?true:false;
    if(is_et){
        EMlog(LOGLEVEL_INFO, "ET mode is ON...\n");
    }else{
        EMlog(LOGLEVEL_INFO, "ET mdoe is OFF...\n");
    }
    
    addsignal(SIGPIPE, SIG_IGN);
    
    //initialize threadpool, thread pool element http_conn type
    threadpool<http_conn> * pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch(...){
        return 1;
    }

    //create listen fd
    int listenfd = c_socket(AF_INET, SOCK_STREAM, 0);
    set_port_reuse(listenfd);
    c_bind(listenfd, port);
    c_listen(listenfd, 128);

    //create epoll object, event array
    epoll_event events[MAX_EVENT];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false, false);
    http_conn::m_epollfd = epollfd;

    c_socketpair(pipefd);
   
    set_socket_nonblock(pipefd[1]);

    addfd(epollfd, pipefd[0], false, false); 
    
    addsignal(SIGALRM, sig_handler);
    addsignal(SIGTERM, sig_handler);
    bool server_close = false;

    //create an array to save all client data
    http_conn * users = new http_conn [MAX_FD];

    bool timeout = false;
    alarm(TIMESLOT);
    
    while(1){
        //detect events
        int active_events = epoll_wait(epollfd, events, MAX_EVENT, -1);
        if(active_events < 0 && errno!= EINTR){
            EMlog(LOGLEVEL_ERROR, "epoll error\n");
            break;
        }

        //traverse event array
        for(int i=0; i<active_events ;i++){
            int sockfd = events[i].data.fd;
            //if sockfd has a event, meaning a clit is trying to connect
            if(sockfd == listenfd){
                struct sockaddr_in clit_addr;
                socklen_t clit_addrlen = sizeof(clit_addr);
                
                int connfd = c_accept(listenfd);
                //if connection number reaches max
                if(http_conn::m_user_count >= MAX_FD){
                //write to clit:server busy
                    close(connfd);
                    continue;
                }
                users[connfd].init(connfd, clit_addr, is_et);
            }else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN)){
                int sig;
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1){
                    continue;
                }else if(ret == 0){
                    continue;
                }else{
                    for(int i = 0; i < ret; ++i){
                        switch (signals[i]) 
                        {
                        case SIGALRM:
                            timeout = true;
                            break;
                        case SIGTERM:
                            server_close = true;
                        }
                    }
                }

            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
            // the peer disconnects , close connction
                EMlog(LOGLEVEL_DEBUG,"EPOLLRDHUP | EPOLLHUP | EPOLLERR\n");
                EMlog(LOGLEVEL_DEBUG, "disconnected!");
                users[sockfd].close_conn();
                //every time a connection is closed, the corresponding timer should be deleted
                http_conn::m_timer_list.delete_timer(users[sockfd].m_timer);
            } else if(events[i].events & EPOLLIN){
                EMlog(LOGLEVEL_DEBUG, "EPOLLIN event occured!");
                //if the active event is read event
                if(users[sockfd].reader()){
                    pool->append(users + sockfd);
                }else {
                    users[sockfd].close_conn();
                    http_conn::m_timer_list.delete_timer(users[sockfd].m_timer);
                }

            } else if(events[i].events &EPOLLOUT){
                EMlog(LOGLEVEL_DEBUG, "EPOLLOUT event occured!");
                if(!users[sockfd].writer()){
                    users[sockfd].close_conn();
                    http_conn::m_timer_list.delete_timer(users[sockfd].m_timer);
                }
            }
        }
        //if there are timeout events
        if(timeout) {
            http_conn::m_timer_list.tick();
            alarm(TIMESLOT);    //reset alarm
            timeout = false;    //timeout events completed, reset timeout

        }  
    }

    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete pool;
    return 0;

}
