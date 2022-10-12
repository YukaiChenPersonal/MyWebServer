#include "http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
sort_timer_list http_conn::m_timer_list;

extern void set_port_reuse(int fd);
extern void set_socket_nonblock(int fd);

//local resource folder directory
const char* file_dir = "/home/yukai/webserver/resource";

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Request has syntax errors.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "No access to the specified file.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "No such file in the server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//add file descriptor to epoll event tree
void addfd(int epollfd, int fd, bool one_shot, bool is_et){
    epoll_event event;
    event.data.fd = fd;
    if(is_et){
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    
    if(one_shot)
        event.events |= EPOLLONESHOT;
    
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //set fd non-block
    set_socket_nonblock(fd);
}

//remove fd from epoll event tree
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd ,0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev){

    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//initialize connction
void http_conn::init(int sockfd, const sockaddr_in& addr, bool is_et){
    m_sockfd = sockfd;
    m_addr = addr;
     //set port reuse
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //add to epoll event tree
    addfd(m_epollfd, m_sockfd, true, is_et);
    m_user_count++;   // update active connection number

    char ip_addr[16] = "";
    const char* str = inet_ntop(AF_INET, &addr.sin_addr.s_addr, ip_addr, sizeof(ip_addr));
    EMlog(LOGLEVEL_INFO, "The No.%d client gets socket fd: %d, it's ip address is: %s.\n", m_user_count, sockfd, str);
    
    init();     //initialize other members

    /*set upa timer, set its callback function and expire time, 
    bind timer and client data,
    add the new timer to the timer linked list*/
    
    timer* self_timer = new timer;
    self_timer->user_data = this;
    time_t cur_time = time(NULL);
    self_timer->expire = cur_time + 10 * TIMESLOT;
    this->m_timer = self_timer;
    m_timer_list.add_timer(self_timer);

}

void http_conn::init(){
    m_ana_state = ANA_STATE_REQUESTLINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_index = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_is_conn = false;
    m_host = 0;
    m_content_length = 0;

    bzero(m_read_buf, READ_BUF_SIZE);
    bzero(m_write_buf, WRITE_BUF_SIZE);
    bzero(m_real_file, FILENAME_MAXLEN);

    m_write_index = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
}

//close connection
void http_conn::close_conn(){
    if(m_sockfd != -1){
        m_user_count--;
        EMlog(LOGLEVEL_INFO, "closing fd: %d, rest user num :%d\n", m_sockfd, m_user_count);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
    }
}

bool http_conn::reader(){
    //loop read client data until cilent close connection
    //update expire time
    if(m_timer){
        time_t cur_time = time(NULL);
        m_timer->expire = cur_time + 10 * TIMESLOT;
        m_timer_list.adjust_timer(m_timer);
    }
    //if data is bigger than buffer size, return false
    if(m_read_index >= READ_BUF_SIZE) {
        return false;
    }
    
    int nread = 0;
    while(1){
        nread = recv(m_sockfd, m_read_buf + m_read_index, READ_BUF_SIZE - m_read_index,0);
        if(nread == -1){
            if(errno == EAGAIN || EWOULDBLOCK){
                //no data!
                break;
            } else {  
                return false;
            }
        } else if(nread == 0){     //peer ends connection
            return false;
        }
        m_read_index += nread;
    }
    EMlog(LOGLEVEL_INFO, "sock_fd = %d read done.\n", m_sockfd);
    return true;
}

//main state machine
http_conn::HTTP_CODE http_conn::process_reader() {
    LINE_STATE line_state = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;
    while((m_ana_state == ANA_STATE_CONTENT && line_state == LINE_OK)||
    (line_state = parse_line())==LINE_OK) {
        
        text = get_line();
        m_start_line = m_checked_index;

        switch(m_ana_state){
            case ANA_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case ANA_STATE_HEADER:
            {
                ret = parse_header(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                } else if(ret==GET_REQUEST){
                    return do_request();
                }
                break;
            }
            case ANA_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_state = LINE_INCOMPLETE;
                break;
            }

            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// parse http request line, get request method, target url and http version
http_conn::HTTP_CODE http_conn::parse_request_line(char * text){
    // GET /idex.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    if(!m_url) return BAD_REQUEST;
    *m_url = '\0';
    m_url++;
    char * method = text;
    if(strcasecmp(method , "GET") == 0){
        m_method = GET;
    }else if(strcasecmp(method , "POST") == 0){
        m_method = POST;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version = '\0';
    m_version++;
    /*
    //webbench test uses http 1.0, so we ignore version check here.
    if(strcasecmp(m_version, "HTTP/1.1")!=0){
        return BAD_REQUEST;
    }
    */
    

    if(strncasecmp(m_url, "http://", 7)==0){
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    m_ana_state = ANA_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_header(char* text){
    //if get an empty line, request header is completely parsed
    if(text[0] == '\0'){
        //check if request body needs to be parsed
        if(m_content_length != 0){
            m_ana_state = ANA_STATE_CONTENT;
            return NO_REQUEST;
        }
        //if not, the request is completely read
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        //deal with "Connection: keep-alive"
        text += 11;
        text += strspn( text, " \t");
         if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_is_conn = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // deal with "Content-Length:..."
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
       #ifdef COUT_OPEN
            EMlog(LOGLEVEL_DEBUG,"Unknow header: %s\n", text );
        #endif   
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char * text){
    if ( m_read_index >= ( m_content_length + m_checked_index ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//pares a line of http request
http_conn::LINE_STATE http_conn::parse_line(){
    char temp;
    for(; m_checked_index <m_read_index; ++m_checked_index){
        temp = m_read_buf[m_checked_index];
        if(temp == '\r'){
            if(m_checked_index +1 == m_read_index){
                return LINE_INCOMPLETE;
            //read "\r\n" means line ends
            } else if(m_read_buf[m_checked_index +1] == '\n'){
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
            //also deals with read "\r\n"
        } else if(temp == '\n'){
            if(m_checked_index>1 && m_read_buf[m_checked_index-1]=='\r'){
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        //return LINE_INCOMPLETE;
    }
    return LINE_INCOMPLETE;
}

http_conn::HTTP_CODE http_conn::do_request(){
    // local resourse dir: /home/yukai/webserver/resourse
    strcpy(m_real_file, file_dir);
    int len = strlen(file_dir);
    strncpy(m_real_file + len, m_url, FILENAME_MAXLEN -len -1);
    EMlog(LOGLEVEL_DEBUG, "file directory: %s.", file_dir);
    //get state of m_real_file
    if(stat(m_real_file, &m_file_stat)<0){
        return NO_RESOURCE;
    }

    //check access level
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    //check if file name is a directory
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    int fd = open( m_real_file, O_RDONLY);
    m_file_addr = (char* )mmap(0, m_file_stat.st_size, PROT_READ ,MAP_PRIVATE, fd, 0 );
    close(fd);
    return FILE_REQUEST;
}

//release memory map
void http_conn::unmap() {
    if( m_file_addr )
    {
        munmap( m_file_addr, m_file_stat.st_size );
        m_file_addr = 0;
    }
}

//The following functions are called by process_writer() to generate http reponse
//line by line  
bool http_conn::add_response( const char* format, ...){
    if(m_write_index >= WRITE_BUF_SIZE) {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf( m_write_buf + m_write_index, WRITE_BUF_SIZE - 1 - m_write_index, format, arg_list );
    if( len >= ( WRITE_BUF_SIZE - 1 - m_write_index ) ) {
        return false;
    }
    m_write_index += len;
    va_end( arg_list );
    return true;
}



bool http_conn::add_state_line(int state, const char* title){
    EMlog(LOGLEVEL_DEBUG,"%s %d %s\r\n", "HTTP/1.1", state, title);     
    return add_response("%s %d %s\r\n", "HTTP/1.1", state, title);
}

bool http_conn::add_header(int content_len){
    add_content_length(content_len);
    add_content_type();
    add_is_conn();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    EMlog(LOGLEVEL_DEBUG,"Content-Length: %d\r\n", content_len);  
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_is_conn()
{  
    EMlog(LOGLEVEL_DEBUG,"Connection: %s\r\n", ( m_is_conn == true ) ? "keep-alive" : "close" );
    return add_response( "Connection: %s\r\n", ( m_is_conn == true )?"keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    EMlog(LOGLEVEL_DEBUG,"%s", "\r\n");    
    return add_response("%s", "\r\n");
}

bool http_conn::add_content_type() {
    EMlog(LOGLEVEL_DEBUG,"Content-Type:%s\r\n", "text/html");  
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

bool http_conn::process_writer(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:
        {
            add_state_line(500,error_500_title);
            add_header( strlen(error_500_form));
            if (!add_content(error_500_form)){
                return false;
            }
            break;
        }

        case BAD_REQUEST:
        {
            add_state_line(400, error_400_title);
            add_header(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        }

        case NO_RESOURCE:
        {
            add_state_line(404, error_404_title);
            add_header(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            } 
        }

        case FORBIDDEN_REQUEST:
        {
            add_state_line(403, error_403_title);
            add_header(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            } 
        }

        case FILE_REQUEST:
        {
            add_state_line(200, ok_200_title);
            add_header(m_file_stat.st_size);
            EMlog(LOGLEVEL_DEBUG, "%s", m_file_addr);
            m_iovec[ 0 ].iov_base = m_write_buf;
            m_iovec[ 0 ].iov_len = m_write_index;
            m_iovec[ 1 ].iov_base = m_file_addr;
            m_iovec[ 1 ].iov_len = m_file_stat.st_size;
            m_iovec_count = 2;

            bytes_to_send = m_write_index + m_file_stat.st_size;

            return true;
            break;
        }

        default:
            return false;
    }

    m_iovec[ 0 ].iov_base = m_write_buf;
    m_iovec[ 0 ].iov_len = m_write_index;
    m_iovec_count = 1;
    return true;
}

bool http_conn::writer()
{
    int temp = 0;

    if(m_timer){
        time_t cur_time = time(NULL);
        m_timer->expire = cur_time +10 * TIMESLOT;
        m_timer_list.adjust_timer(m_timer);
    }
    EMlog(LOGLEVEL_INFO, "client with socket fd %d is writing %d bytes.\n", m_sockfd, bytes_to_send); 
    if ( bytes_to_send == 0 ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        temp = writev(m_sockfd, m_iovec, m_iovec_count);
        if ( temp <= -1 ) {
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iovec[0].iov_len)
        {
            m_iovec[0].iov_len = 0;
            m_iovec[1].iov_base = m_file_addr + (bytes_have_send - m_write_index);
            m_iovec[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iovec[0].iov_base = m_write_buf + bytes_have_send;
            m_iovec[0].iov_len = m_iovec[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            //no more data to send
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_is_conn)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

//called by working threads in the thread pool, api for processing http requests
void http_conn::process(){
    EMlog(LOGLEVEL_DEBUG, "parsing request, generate response.\n");
    HTTP_CODE read_ret = process_reader();
    
    //if request incomplete, needs to read this request again
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    //if a request is complete, generate response
    bool write_ret = process_writer(read_ret);

    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);

}