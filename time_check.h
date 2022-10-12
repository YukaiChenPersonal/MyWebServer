//A double linked list based timer for closing timed out clients


#ifndef TIMER_CHECK
#define TIMER_CHECK

#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include "locker.h"
#include "http_conn.h"
#include "log.h"

#define BUF_SIZ 128

class http_conn;
class timer {
public:
    time_t expire;        //client time out time 
    http_conn* user_data;
    timer* prev;
    timer* next;

    timer() :prev(NULL), next(NULL){}
};

//timer linked list, ascending, double way, with head node and tail node
class sort_timer_list{
private:
    timer* head;
    timer* tail;

    //add a new timer at the next position of a existing timer
    void add_timer(timer* atimer, timer* list_head);


public:
    sort_timer_list(): head(NULL), tail(NULL) {}
    //destructor needs to delete all timers in the linked list when destruction
    ~sort_timer_list() {
        timer* tmp = head;
        while(tmp!=NULL){
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    
    //add timer to linked list
    void add_timer(timer* atimer);

    void adjust_timer(timer* atimer);

    void delete_timer(timer* atimer);

    //check timed out clients on the linked list
    void tick();
};



#endif