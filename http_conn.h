#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>
#include "time_check.h"
#include "log.h"
#include "socket_wrap.h"

#define TIMESLOT 5
const bool ET = true;

class sort_timer_list;
class timer;


class http_conn{
public:

    static int m_epollfd;   //register all socket event to the same epoll event tree
    static int m_user_count;     //count number of active user
    static const int READ_BUF_SIZE = 2048;    //size of read buffer
    static const int WRITE_BUF_SIZE = 1024;    //size of write buffer
    static const int FILENAME_MAXLEN = 200;    //max file name length
    static sort_timer_list m_timer_list;

    timer * m_timer;

    enum METHOD {GET=0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    //defines states used when analysing http header
    enum ANA_STATE {ANA_STATE_REQUESTLINE =0, ANA_STATE_HEADER, ANA_STATE_CONTENT};
    //define states used when reading a single line.
    enum LINE_STATE {LINE_OK =0, LINE_BAD, LINE_INCOMPLETE};

    /*define states used when request is analyised
    NO_REQUEST: request is incomplete, needs reading the rest if hte request
    GET_REQUEST: read a complete http request
    BAD_REQUEST: The client request has syntax errors
    NO_RESOURCE: The server has no resource to answer a client request
    FORBIDDEN_REQUEST: The client does not have sufficient access to the resource
    FILE_REQUEST: File get sucess
    INTERNAL_ERROR: Internal error of the server
    CLOSED_CONNECTION: The client terminates the connection.
    */

   enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST,
    FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    

    http_conn(){}
    ~http_conn(){}
    
    void process(); //process request sent by client
    void init(int sockfd, const sockaddr_in & addr, bool is_et);    //initialize new connection
    void close_conn();    //close connection

    bool reader();    //non-block read
    bool writer();    //non-block write

private:
    int m_sockfd;        //bind a socket for every http request
    sockaddr_in m_addr;  //address for this socket
    char m_read_buf[READ_BUF_SIZE];
    int m_read_index;    //next index of data that have already been read
    char m_write_buf[WRITE_BUF_SIZE];
    int m_write_index;
    char * m_file_addr;

    int m_checked_index; //location of the char that is being parsed currently
    int m_start_line;    //start location of current line being parsed

    ANA_STATE m_ana_state;

    char * m_url;         //name of requested file
    char * m_version;     //http version
    METHOD m_method;      //request method
    char * m_host;        //
    bool m_is_conn;       //Determine whether the http request should stay connected
    int m_content_length; //total length of the http request

    char m_real_file[FILENAME_MAXLEN];
    struct stat m_file_stat;
    struct iovec m_iovec[2];
    int m_iovec_count;

    int bytes_to_send;
    int bytes_have_send;

    void init();

    HTTP_CODE process_reader();    //parse http requests
    bool process_writer(HTTP_CODE ret); 

    HTTP_CODE parse_request_line(char * text); //parse request line
    HTTP_CODE parse_header(char* text);    //parse request header
    HTTP_CODE parse_content(char * text);    //parse request content
    HTTP_CODE do_request();

    LINE_STATE parse_line();    //parse a single line
    char* get_line() { return m_read_buf + m_start_line;}

    void unmap();
    //The following functions are called by process_writer to compose the http
    //response line by line
    bool add_state_line(int state, const char* title);    //add response state line
    bool add_response(const char* format, ...);
    bool add_header(int content_length);
    bool add_content_length(int content_length);
    bool add_is_conn();
    bool add_blank_line();
    bool add_content_type();
    bool add_content(const char* content);
};
#endif
