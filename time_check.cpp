#include "time_check.h"

void sort_timer_list::add_timer(timer *atimer, timer *list_head)
{
    timer *cur = list_head;
    timer *tmp = cur->next;

    while (tmp != NULL)
    {
        if (atimer->expire < tmp->expire)
        {
            cur->next = atimer;
            atimer->next = tmp;
            tmp->prev = atimer;
            atimer->prev = cur;
        }
        cur = tmp;
        tmp = tmp->next;
    }

    if (!tmp)
    {
        cur->next = atimer;
        atimer->prev = cur;
        atimer->next = NULL;
        tail = atimer;
    }
}

// add timer to linked list
void sort_timer_list::add_timer(timer *atimer)
{
    // if arguement is NULL, do nothing
    if (!atimer)
    {
        return;
    }
    // if linked list is empty, set head and tail as timer
    if (!head)
    {
        head = tail = atimer;
        return;
    }

    if (atimer->expire < head->expire)
    {
        atimer->next = head;
        head->prev = atimer;
        head = atimer;
        return;
    }
    // if the arguement's expire value is smaller than the head, add arguemnt at head
    // else, use the overloaded version of add_timer to add at other position.
    add_timer(atimer, head);
}

void sort_timer_list::adjust_timer(timer *atimer)
{
    if (!atimer)
    {
        return;
    }

    timer *tmp = atimer->next;
    // if this timer is tail or its expire is still smalller than the next timer
    // after modification, do nothing
    if (!tmp || atimer->expire < tmp->expire)
    {
        return;
    }
    else if (atimer == head)
    { // if this timer is head, take it out and add again
        head = head->next;
        head->prev = NULL;
        atimer->next = NULL;
        add_timer(atimer, head);
    }
    else
    {
        atimer->prev->next = atimer->next;
        atimer->next->prev = atimer->prev;
        add_timer(atimer, atimer->next);
    }
}

void sort_timer_list::delete_timer(timer *atimer)
{
    if (!atimer)
    {
        return;
    }

    if (atimer == head && atimer == tail)
    {
        delete atimer;
        head = NULL;
        tail = NULL;
        return;
    }

    if (atimer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete atimer;
        return;
    }

    if (atimer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete atimer;
        return;
    }

    atimer->prev->next = atimer->next;
    atimer->next->prev = atimer->prev;
    delete atimer;
}

// check timed out clients on the linked list
void sort_timer_list::tick()
{
    if (!head)
    {
        return;
    }
    printf("timer tick\n");
    time_t cur = time(NULL);
    timer *tmp = head;
    // traverse the linked list from head, until finds a timer that has not expired
    while (tmp)
    {
        if (cur < tmp->expire)
        {
            break;
        }
        http_conn *cur_clit = tmp->user_data;
        cur_clit->close_conn();
        delete_timer(tmp);
        tmp = head;
    }
}