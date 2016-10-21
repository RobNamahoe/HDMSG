//
//  HdmsgHost.h
//  HDMSG
//
//  Created by ics699 on 9/21/16.
//  Copyright © 2016 ics699. All rights reserved.
//

#ifndef HDMSGHOST_H
#define HDMSGHOST_H

#include <stdio.h>
#include "msg/msg.h"  

//////////////////////
// Constants
//////////////////////
extern int SHUFFLERS_PER_REDUCER;

extern int mappers_per_worker;
extern int reducers_per_worker;
extern int number_of_workers;

extern xbt_dict_t hosts;

//////////////////////
// Types
//////////////////////

struct HdmsgHost
{
    int host_id;
    int is_master;
    int is_worker;
 
    int active_mappers;
    
    xbt_fifo_t map_tasks;
    xbt_fifo_t reduce_tasks;
    xbt_fifo_t shuffle_tasks;
    
    const char *host_name;
    
    msg_host_t host;
    
    xbt_fifo_t mappers;     // = mappers_per_worker
    xbt_fifo_t reducers;    // = reducers_per_worker
    
    xbt_fifo_t shuffle_senders;

};


//////////////////////
// Prototypes
//////////////////////
struct HdmsgHost *newHdmsgHost(int, msg_host_t, char *);
void add_map_task(struct HdmsgHost *, double);
void partition_map_task(struct HdmsgHost *, double);
void activate_mappers(struct HdmsgHost*);
void activate_reducers(struct HdmsgHost *);
void destroyHdmsgHost(struct HdmsgHost *);

#endif /* HdmsgHost_h */
