//
//  HdmsgHost.c
//  HDMSG
//
//  Created by Robert Namahoe on 9/25/16.
//  Copyright © 2016 ics699. All rights reserved.
//

#include <stdio.h>
#include "HdmsgHost.h"

struct HdmsgHost *newHdmsgHost(int host_id, msg_host_t msg_host, char * attributes)
{
    struct HdmsgHost *this_host = malloc(sizeof(struct HdmsgHost));
    
    this_host->host_id = host_id;
    this_host->host = msg_host;
    this_host->host_name = MSG_host_get_name(msg_host);
    
    this_host->is_master = (strstr(attributes, "master") == NULL) ? 0 : 1;
    this_host->is_worker = (strstr(attributes, "worker") == NULL) ? 0 : 1;
    
    // Processes
    this_host->active_mappers = 0;
    this_host->mappers = xbt_fifo_new();
    this_host->reducers = xbt_fifo_new();
    this_host->shuffle_senders = xbt_fifo_new();
    
    // Work queues
    this_host->map_tasks = xbt_fifo_new();
    this_host->shuffle_tasks = xbt_fifo_new();
    this_host->reduce_tasks = xbt_fifo_new();
    
    return this_host;
}

void add_map_task(struct HdmsgHost * this_host, double compute_cost)
{
    msg_task_t map_task = MSG_task_create("map", compute_cost, 0, NULL);
    xbt_fifo_push(this_host->map_tasks, map_task);
    return;
}

void partition_map_task(struct HdmsgHost *this_host, double communication_cost)
{
    int i;
    char * key;
    struct HdmsgHost * other_host;
    xbt_dict_cursor_t cursor = NULL;
    
    // Create a shuffle task for each reducer and store in the shuffle_tasks work queue
    xbt_dict_foreach(hosts, cursor, key, other_host)
    {
        if (other_host->is_worker)
        {
            for (i = 0; i < xbt_fifo_size(other_host->reducers); i++)
            {
                msg_task_t shuffle_task = MSG_task_create("shuffle", 0, communication_cost, other_host->host);
                xbt_fifo_push(this_host->shuffle_tasks, shuffle_task);
            }
        }
    }
    
    return;
}

void activate_mappers(struct HdmsgHost *this_host)
{
    xbt_fifo_item_t bucket;
    msg_process_t mapper = NULL;
    
    xbt_fifo_foreach(this_host->mappers, bucket, mapper, msg_process_t)
    {
        MSG_process_resume(mapper);
    }
    
    return;
}



void activate_reducers(struct HdmsgHost *this_host)
{
    xbt_fifo_item_t bucket;
    msg_process_t reducer = NULL;
    
    xbt_fifo_foreach(this_host->reducers, bucket, reducer, msg_process_t)
    {
        MSG_process_resume(reducer);
    }
    
    return;
}


void destroyHdmsgHost(struct HdmsgHost *this_host)
{
    printf("I am destroying: %s\n", this_host->host_name);
    return;
}
