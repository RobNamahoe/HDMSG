/* Copyright (c) 2010-2014. The SimGrid Team.
 * All rights reserved.                                                     */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

#include "HdmsgHost.h"

#include "simgrid/msg.h"
#include "xbt/sysdep.h"

/* Create a log channel to have nice outputs. */
#include "xbt/log.h"
#include "xbt/asserts.h"

XBT_LOG_NEW_DEFAULT_CATEGORY(hdmsgCat, "Messages specific for this msg application");

/* Prototypes */
double get_initialization_cost(msg_host_t);
double get_map_cost(msg_host_t);
double get_bytes_to_shuffle();
double get_reduce_cost(msg_host_t);
double Log2(double);
void distributeHdfsChunks();

/* Process Prototypes */
int master(int argc, char *argv[]);
int initializeProcs(int argc, char * argv[]);
int map(int argc, char * argv[]);
int shuffleSend(int argc, char * argv[]);
int shuffleReceive(int argc, char * argv[]);
int reduce(int argc, char * argv[]);

/* Constants */
int SHUFFLERS_PER_REDUCER = 5;
int SHUFFLE_SLEEP_DURATION = 1;  // In seconds

/* Globals */
double MAP_CALIBRATION_FACTOR;
double REDUCE_CALIBRATION_FACTOR;

xbt_dict_t hosts;
xbt_dict_t host_attributes;

int number_of_hosts;
int number_of_workers;

long mappers;
long reducers;

long input_size;
long input_size_bytes;

long hdfs_chunk_size;
long hdfs_chunk_size_bytes;

double sim_map;
double sim_reduce;

int shuffle_started;

int ready_inits;
int ready_mappers;
int ready_shuffleSenders;
int ready_reducers;

/* Master Process */
int master(int argc, char *argv[])
{
    char * key;
    struct HdmsgHost *hdmsg_host;
    xbt_dict_cursor_t cursor = NULL;
    
    int i = 0;
    
    long remaining_inits = 0;
    long remaining_mappers = 0;
    long remaining_shufflers = 0;
    long remaining_reducers = 0;
    long expected_messages = 0;
    
    msg_comm_t res_irecv;
    msg_task_t task_com;
    msg_task_t *tasks = xbt_new(msg_task_t, number_of_workers);
    xbt_dynar_t comms = xbt_dynar_new(sizeof(msg_comm_t), NULL);
    
    XBT_INFO("INITIALIZATION BEGIN");
    
    // Initialize processes (mappers, shufflers, and reducers) on each host
    xbt_dict_foreach(hosts, cursor, key, hdmsg_host)
    {
        if (hdmsg_host->is_worker)
        {
            MSG_process_create("Init", initializeProcs, NULL, hdmsg_host->host);
            
            tasks[remaining_inits] = NULL;
            res_irecv = MSG_task_irecv(&tasks[remaining_inits], "master");
            xbt_dynar_push_as(comms, msg_comm_t, res_irecv);
            remaining_inits++;
        }
    }
    
    while (!xbt_dynar_is_empty(comms))
    {
        xbt_dynar_remove_at(comms, MSG_comm_waitany(comms), &res_irecv);
        task_com = MSG_comm_get_task(res_irecv);
        
        if (!strcmp(MSG_task_get_name(task_com), "init_exit"))
        {
            msg_host_t h = MSG_task_get_source(task_com);
            MSG_task_destroy(task_com);
            
            const char *host_name = MSG_host_get_name(h);
            struct HdmsgHost *hdmsg_host = xbt_dict_get(hosts, host_name);
            
            remaining_mappers += get_mapper_count(hdmsg_host);
            remaining_shufflers += get_shuffler_count(hdmsg_host);
            remaining_reducers += get_reducer_count(hdmsg_host);
            
            remaining_inits--;
            
            if (remaining_inits == 0)
            {
                XBT_INFO("INITIALIZATION COMPLETE");
                
                // Add an extra message to account for the message sent when the shuffle phase begins
                expected_messages = 1 + remaining_mappers + remaining_shufflers + remaining_reducers;
                
                free(tasks);
                tasks = xbt_new(msg_task_t, expected_messages);
                
                for (i = 0; i < expected_messages; i++)
                {
                    tasks[i] = NULL;
                    res_irecv = MSG_task_irecv(&tasks[i], "master");
                    xbt_dynar_push_as(comms, msg_comm_t, res_irecv);
                }
                
                XBT_INFO("MAP PHASE BEGIN");
                
                // Activate Mappers
                xbt_dict_foreach(hosts, cursor, key, hdmsg_host)
                {
                    activate_mappers(hdmsg_host);
                }
            }
        }
        else if (!strcmp(MSG_task_get_name(task_com), "shuffle_start"))
        {
            XBT_INFO("SHUFFLE PHASE BEGIN");
        }
        else if (!strcmp(MSG_task_get_name(task_com), "map_exit"))
        {
            remaining_mappers--;
            if (remaining_mappers == 0)
            {
                XBT_INFO("MAP PHASE COMPLETE");
            }
        }
        else if (!strcmp(MSG_task_get_name(task_com), "shuffle_exit"))
        {
            remaining_shufflers--;
            if (remaining_shufflers == 0)
            {
                XBT_INFO("SHUFFLE PHASE COMPLETE");
                XBT_INFO("REDUCE PHASE BEGIN");
                
                // Activate Reducers
                xbt_dict_foreach(hosts, cursor, key, hdmsg_host)
                {
                    activate_reducers(hdmsg_host);
                }
            }
        }
        else if (!strcmp(MSG_task_get_name(task_com), "reduce_exit"))
        {
            remaining_reducers--;
            if (remaining_reducers == 0)
            {
                XBT_INFO("REDUCE PHASE COMPLETE");
            }
        }
        else
        {
            printf("*** MAP PHASE ERROR Received unexpected task: %s\n", MSG_task_get_name(task_com));
        }
    }
    
    free(tasks);
    
    return 0;
}                               /* end_of_master */

/** Initialize Processes */
int initializeProcs(int argc, char * argv[])
{
    // Get the current host
    const char * host_name = MSG_host_get_name(MSG_process_get_host(NULL));
    struct HdmsgHost * this_host = xbt_dict_get(hosts, host_name);
    
    int i;
    long mappers_to_launch = MSG_host_get_core_number(this_host->host);
    long reducers_to_launch = reducers / number_of_workers;
    
    // If the number of reducers is not divisible by the number of workers,
    // allocate the remaining reducers
    if (reducers % number_of_workers != 0)
    {
        if (this_host->host_id <= (reducers % number_of_workers))
        {
            reducers_to_launch++;
        }
    }
    
    // Create mappers
    mappers += mappers_to_launch;
    for (i = 0; i < mappers_to_launch; i++)
    {
        char * mapper_name = bprintf("%s-Mapper-%d", host_name, i);
        msg_process_t mapper = MSG_process_create(mapper_name, map, NULL, this_host->host);
        xbt_fifo_push(this_host->mappers, mapper);
        this_host->active_mappers++;
    }
    
    // Create shufflers
    long number_of_shufflers = SHUFFLERS_PER_REDUCER * reducers_to_launch;
    for (i = 0; i < number_of_shufflers; i++)
    {
        char * sender_name = bprintf("%s-Sender-%d", host_name, i);
        msg_process_t sender = MSG_process_create(sender_name, shuffleSend, NULL, this_host->host);
        xbt_fifo_push(this_host->shuffle_senders, sender);
    }
    
    // Create reducers
    for (i = 0; i < reducers_to_launch; i++)
    {
        char * reducer_name = bprintf("%s-Reducer", host_name);
        msg_process_t reducer = MSG_process_create(reducer_name, reduce, NULL, this_host->host);
        xbt_fifo_push(this_host->reducers, reducer);
    }
    
    // The cost of this task should be equal to the overhead of starting these processes
    MSG_task_execute(MSG_task_create("initialization", get_initialization_cost(this_host->host), 0, NULL));
    
    // Notify master that initialization on this host is complete
    MSG_task_send(MSG_task_create("init_exit", 0, 1, NULL), "master");
    
    return 0;
}

/** Map Process */
int map(int argc, char * argv[])
{
    double start_time;
    
    double bytes_to_shuffle = get_bytes_to_shuffle();
    MSG_process_suspend(MSG_process_self());
    
    msg_host_t msg_host = MSG_process_get_host(MSG_process_self());
    struct HdmsgHost * this_host = xbt_dict_get(hosts, MSG_host_get_name(msg_host));
    
    while (xbt_fifo_size(this_host->map_tasks) > 0)
    {
        // Do map tasks
        msg_task_t map_task = xbt_fifo_pop(this_host->map_tasks);
        
        if (map_task != NULL)
        {
            XBT_INFO("%s is starting a map task", MSG_process_get_name(MSG_process_self()));
            start_time = MSG_get_clock();
            MSG_task_execute(map_task);
            sim_map += MSG_get_clock() - start_time;
            MSG_task_destroy(map_task);
            XBT_INFO("%s has completed a map task", MSG_process_get_name(MSG_process_self()));
            
            // Partition map output for shufflers to retrieve
            partition_map_task(this_host, bytes_to_shuffle);
        }
    }
    
    this_host->active_mappers--;
    
    // Notify master that I'm done working
    msg_comm_t comm = MSG_task_isend(MSG_task_create("map_exit", 0, 1, NULL), "master");
    MSG_comm_wait(comm, -1);
    MSG_comm_destroy(comm);
    
    return 0;
}


/** Shuffle Send Process */
int shuffleSend(int argc, char * argv[])
{
    msg_task_t task = NULL;
    
    const char * process_name = MSG_process_get_name(MSG_process_self());
    
    msg_host_t msg_host = MSG_process_get_host(MSG_process_self());
    struct HdmsgHost * this_host = xbt_dict_get(hosts, MSG_host_get_name(msg_host));
    
    while (1)
    {
        task = xbt_fifo_shift(this_host->shuffle_tasks);
        
        if (task != NULL)
        {
            // If this is the first shuffle task, notify the master so the event is logged to the console
            if (!shuffle_started)
            {
                shuffle_started = 1;
                MSG_task_dsend(MSG_task_create("shuffle_start", 0, 1, NULL), "master", NULL);
            }
            
            // Create a shuffle receiver on the recipient host
            msg_host_t recipient_host = MSG_task_get_data(task);
            char * receiver_name = bprintf("%s->%s-Receiver", process_name, MSG_host_get_name(recipient_host));
            MSG_process_create(receiver_name, shuffleReceive, NULL, recipient_host);
            
            // Send the task to the shuffle receiver
            XBT_INFO("%s is starting a shuffle task", MSG_process_get_name(MSG_process_self()));
            MSG_task_send(task, receiver_name);
            XBT_INFO("%s has completed a shuffle task", MSG_process_get_name(MSG_process_self()));
        }
        else
        {
            if (this_host->active_mappers > 0)
            {
                // If there are still active mappers, sleep then check for more work
                MSG_process_sleep(SHUFFLE_SLEEP_DURATION);
            }
            else
            {
                // Else no mappers -> no further tasks, exit
                break;
            }
        }
    }
    
    // Notify master that I'm done working
    msg_comm_t comm = MSG_task_isend(MSG_task_create("shuffle_exit", 0, 1, NULL), "master");
    MSG_comm_wait(comm, -1);
    MSG_comm_destroy(comm);
    
    return 0;
}


/** Shuffle Receive Process */
int shuffleReceive(int argc, char * argv[])
{
    int res;
    msg_task_t task = NULL;
    
    res = MSG_task_receive(&(task), MSG_process_get_name(MSG_process_self()));
    xbt_assert(res == MSG_OK, "MSG_task_get failed: Shuffle Receive");
    MSG_task_destroy(task);
    
    return 0;
}

/** Reduce Process */
int reduce(int argc, char * argv[])
{
    double start_time;
    
    // Wait for the reduce phase to begin
    MSG_process_suspend(MSG_process_self());
    
    XBT_INFO("%s is starting a reduce task", MSG_process_get_name(MSG_process_self()));
    start_time = MSG_get_clock();
    MSG_task_execute(MSG_task_create("reduce", get_reduce_cost(MSG_host_self()), 0, NULL));
    sim_reduce += MSG_get_clock() - start_time;
    XBT_INFO("%s has completed a reduce task", MSG_process_get_name(MSG_process_self()));
    
    // Notify the master that I'm done working
    msg_comm_t comm = MSG_task_isend(MSG_task_create("reduce_exit", 0, 1, NULL), "master");
    MSG_comm_wait(comm, -1);
    MSG_comm_destroy(comm);
    
    return 0;
}

/** Main function */
int main(int argc, char *argv[])
{
    int i, BYTES_PER_MEGABYTE = 1048576;
    char *config_path;
    char *platform_path;
    
    msg_error_t res = MSG_OK;
    MSG_init(&argc, argv);
    
    if (argc != 5)
    {
        printf("Usage: %s map_cf reduce_cf config platform.xml\n", argv[0]);
        printf("Example: %s 0.28 0.29 path_to_config path_to_platform.xml \n", argv[0]);
        exit(1);
    }
    
    // Set calibration factors
    sscanf(argv[1], "%lf", &MAP_CALIBRATION_FACTOR);
    sscanf(argv[2], "%lf", &REDUCE_CALIBRATION_FACTOR);
    
    // Set file paths
    config_path = malloc(strlen(argv[3]) * sizeof(char));
    strcpy(config_path, argv[3]);
    
    platform_path = malloc(strlen(argv[4]) * sizeof(char));
    strcpy(platform_path, argv[4]);
    
    // Register the functions
    MSG_function_register("master", master);
    MSG_function_register("initializeProcs", initializeProcs);
    
    MSG_function_register("map", map);
    MSG_function_register("reduce", reduce);
    
    MSG_function_register("shuffleSend", shuffleSend);
    MSG_function_register("shuffleReceive", shuffleReceive);
    
    // Create the environment
    MSG_create_environment(platform_path);
    
    // Read config file and set parameters
    FILE * config_file = fopen(config_path, "r");
    
    if (config_file == NULL)
    {
        fprintf(stderr, "Error while opening config file.\n");
        exit(1);
    }
    
    char line[256];
    int number_of_masters = 0;
    
    hosts = xbt_dict_new();
    host_attributes = xbt_dict_new();
    
    while (fgets(line, 256, config_file) != NULL)
    {
        line[strcspn(line, "\n")] = 0;
        
        if (strlen(line) > 0)
        {
            char *line_cpy = malloc(strlen(line) * sizeof(char));
            strcpy(line_cpy, line);
            
            char *key = strsep(&line_cpy, " ");
            char *value = strsep(&line_cpy, " ");
            
            if (strcmp(key, "master") == 0 || strcmp(key, "worker") == 0)
            {
                char **hosts;
                int hosts_length = 0;
                
                char *current_attributes = NULL;
                char *new_attributes = NULL;
                
                if (strchr(value, '-') != NULL)
                {
                    char *host1 = strsep(&value, "-"); // returns first host in range
                    char *host2 = strsep(&value, "-"); // returns last host in range
                    
                    char *prefix = malloc(strlen(host1) * sizeof(char));
                    strcpy(prefix, host1);
                    
                    for (i = 0; i < strlen(prefix); i++)
                    {
                        prefix[i] = (isdigit(prefix[i])) ? '\0' : prefix[i];
                    }
                    
                    int loopStart = atoi(&host1[strlen(prefix)]);
                    int loopEnd = atoi(&host2[strlen(prefix)]);
                    
                    hosts_length = loopEnd - loopStart + 1;
                    hosts = malloc((hosts_length) * sizeof(char *));
                    
                    for (i = 0; i < hosts_length; i++)
                    {
                        hosts[i] = malloc(strlen(host2) * sizeof(char));
                        sprintf(hosts[i], "%s%d", prefix, loopStart + i);
                    }
                }
                else
                {
                    hosts = malloc(sizeof(char *));
                    hosts[0] = malloc(strlen(key) * sizeof(char));
                    sprintf(hosts[0], "%s", value);
                    hosts_length = 1;
                }
                
                for (i = 0; i < hosts_length; i++)
                {
                    number_of_masters += (strcmp(key, "master") == 0) ? 1 : 0;
                    number_of_workers += (strcmp(key, "worker") == 0) ? 1 : 0;
                    
                    current_attributes = xbt_dict_get_or_null(host_attributes, hosts[i]);
                    
                    if (current_attributes != NULL)
                    {
                        new_attributes = malloc((strlen(key) + strlen(current_attributes) + 2) * sizeof(char));
                        strcat(new_attributes, current_attributes);
                        strcat(new_attributes, " ");
                    }
                    else
                    {
                        new_attributes = malloc(strlen(hosts[i]) + sizeof(char));
                    }
                    
                    strcat(new_attributes, key);
                    xbt_dict_set(host_attributes, hosts[i], new_attributes, NULL);
                }
                
            }
            else if (strcmp(key, "mappers") == 0)
            {
                if (isdigit(*value))
                {
                    mappers = atoi(value);
                }
            }
            else if (strcmp(key, "reducers") == 0)
            {
                if (isdigit(*value))
                {
                    reducers = atoi(value);
                }
            }
            else if (strcmp(key, "input_size_in_mb") == 0)
            {
                if (isdigit(*value))
                {
                    input_size = atol(value);
                    input_size_bytes = input_size * BYTES_PER_MEGABYTE;
                }
            }
            else if (strcmp(key, "hdfs_chunk_size_in_mb") == 0)
            {
                if (isdigit(*value))
                {
                    hdfs_chunk_size = atoi(value);
                    hdfs_chunk_size_bytes = hdfs_chunk_size * BYTES_PER_MEGABYTE;
                }
            }
            
            // Freeing 'key' works since 'key' will always point to the address returned by
            // malloc whereas the value of line_cpy changes as a result of the call to strsep.
            free(key);
        }
    }
    
    fclose(config_file);
    
    // Must have exactly one master process
    if (number_of_masters != 1)
    {
        fprintf(stderr, "There must be exactly one master process.\n");
        exit(1);
    }
    
    xbt_dynar_t host_dynar = MSG_hosts_as_dynar();
    number_of_hosts = (int) xbt_dynar_length(host_dynar);
    
    XBT_INFO("Got %ldMB input, %ldMB chunks, %ld mappers, %ld reducers, and %d hosts\n",
             input_size,
             hdfs_chunk_size,
             mappers,
             reducers,
             number_of_hosts);
    
    // Now that the platform environment has been created, associate each hdmsg_host with an actual msg_host_t
    int host_id = 1;
    unsigned int cpt;
    msg_host_t dyn_host;
    
    xbt_dynar_foreach (host_dynar, cpt, dyn_host)
    {
        char *attributes = xbt_dict_get_or_null(host_attributes, MSG_host_get_name(dyn_host));
        
        if (attributes != NULL)
        {
            struct HdmsgHost *hdmsg_host = newHdmsgHost(0, dyn_host, attributes);
            
            if (hdmsg_host->is_master)
            {
                hdmsg_host->host_id = 0;
                MSG_process_create("master", master, NULL, hdmsg_host->host);
            }
            else
            {
                hdmsg_host->host_id = host_id;
                host_id++;
            }
            
            xbt_dict_set(hosts, hdmsg_host->host_name, hdmsg_host, (void *)destroyHdmsgHost);
        }
    }
    
    // TODO: Should I ensure that each hdmsg_host has an msg_host_t?
    
    distributeHdfsChunks();
    
    res = MSG_main();
    
    double simulation_time = MSG_get_clock();
    XBT_INFO("Simulation time %g", simulation_time);
    
    sim_map /= (input_size_bytes / hdfs_chunk_size_bytes); // (input_size_bytes / hdfs_chunk_size_bytes) = number of map tasks
    sim_reduce /= reducers;
    
    int iX, iY, iZ;
    iX = log2(input_size) - 8;
    iY = log2(hdfs_chunk_size) - 5;
    iZ = log2(reducers) - 2;
    
    // If I don't have actual execution times, then don't print stats just exit.
    if (iX >= 3 || iY >= 3 || iZ >= 3) { return (res == MSG_OK) ? 0 : 1; }
    
    double mapTimes[3][3][3];  // Input size (256, 512, 1024), Chunk size (32, 64, 128), Number of reducers (4, 8, 16)
    
    /*  256  */
    mapTimes[0][0][0] = 399; // 256-32-4
    mapTimes[0][0][1] = 391;  // 256-32-8
    mapTimes[0][0][2] = 385;  // 256-32-16
    
    mapTimes[0][1][0] = 830; // 256-64-4
    mapTimes[0][1][1] = 815;  // 256-64-8
    mapTimes[0][1][2] = 801;  // 256-64-16
    
    /*  512 */
    mapTimes[0][0][0] = 399; // 256-32-4
    mapTimes[0][0][1] = 391;  // 256-32-8
    mapTimes[0][0][2] = 385;  // 256-32-16
    
    mapTimes[1][0][0] = 438; // 512-32-4
    mapTimes[1][0][1] = 432;  // 512-32-8
    mapTimes[1][0][2] = 425;  // 512-32-16
    
    mapTimes[1][1][0] = 833; // 512-64-4
    mapTimes[1][1][1] = 821; // 512-64-8
    mapTimes[1][1][2] = 809; // 512-64-16
    
    mapTimes[1][2][0] = 1638; // 512-128-4
    mapTimes[1][2][1] = 1616; // 512-128-8
    mapTimes[1][2][2] = 998; // 512-128-16
    
    
    double reduceTimes[3][3][3];
    
    /*  256  */
    reduceTimes[0][0][0] = 334; // 256-32-4
    reduceTimes[0][0][1] = 168;  // 256-32-8
    reduceTimes[0][0][2] = 91;  // 256-32-16
    
    reduceTimes[0][1][0] = 334; // 256-64-4
    reduceTimes[0][1][1] = 171;  // 256-64-8
    reduceTimes[0][1][2] = 90;  // 256-64-16
    
    /*  512 */
    reduceTimes[0][0][0] = 334; // 256-32-4
    reduceTimes[0][0][1] = 168;  // 256-32-8
    reduceTimes[0][0][2] = 91;  // 256-32-16
    
    reduceTimes[1][0][0] = 672; // 512-32-4
    reduceTimes[1][0][1] = 338;  // 512-32-8
    reduceTimes[1][0][2] = 187;  // 512-32-16
    
    reduceTimes[1][1][0] = 664; // 512-64-4
    reduceTimes[1][1][1] = 342; // 512-64-8
    reduceTimes[1][1][2] = 184; // 512-64-16
    
    reduceTimes[1][2][0] = 667; // 512-128-4
    reduceTimes[1][2][1] = 338; // 512-128-8
    reduceTimes[1][2][2] = 183; // 512-128-16
    
    
    double actualTimes[3][3][3];
    
    /*  256  */
    actualTimes[0][0][0] = 773; // 256-32-4
    actualTimes[0][0][1] = 600;  // 256-32-8
    actualTimes[0][0][2] = 527;  // 256-32-16
    
    actualTimes[0][1][0] = 1216; // 256-64-4
    actualTimes[0][1][1] = 1035;  // 256-64-8
    actualTimes[0][1][2] = 947;  // 256-64-16
    
    /*  512 */
    actualTimes[0][0][0] = 773; // 256-32-4
    actualTimes[0][0][1] = 600;  // 256-32-8
    actualTimes[0][0][2] = 527;  // 256-32-16
    
    actualTimes[1][0][0] = 1172; // 512-32-4
    actualTimes[1][0][1] = 833;  // 512-32-8
    actualTimes[1][0][2] = 682;  // 512-32-16
    
    actualTimes[1][1][0] = 1562; // 512-64-4
    actualTimes[1][1][1] = 1221; // 512-64-8
    actualTimes[1][1][2] = 1126; // 512-64-16
    
    actualTimes[1][2][0] = 2237; // 512-128-4
    actualTimes[1][2][1] = 2031; // 512-128-8
    actualTimes[1][2][2] = 1851; // 512-128-16
    
    double actual_map = mapTimes[iX][iY][iZ];
    double actual_reduce = reduceTimes[iX][iY][iZ];
    double actual_exec = actualTimes[iX][iY][iZ];
    
    double sum_of_diffs = 100 * fabs(sim_map - actual_map)/actual_map + 100 * fabs(sim_reduce - actual_reduce)/actual_reduce;
    double avg_percent_diff = sum_of_diffs / 2;
    
    double sim_err = (fabs(simulation_time - actual_exec) / actual_exec) * 100;
    
    // Write results to file
    FILE * output_file = NULL;
    output_file = fopen("HDMSG_output.txt", "a");
    
    fprintf(output_file, "%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
            MAP_CALIBRATION_FACTOR,
            REDUCE_CALIBRATION_FACTOR,
            sim_map,
            actual_map,
            sim_reduce,
            actual_reduce,
            simulation_time,
            actual_exec,
            sim_err,
            sum_of_diffs,
            avg_percent_diff);
    
    fclose(output_file);
    
    // Write results to the console
    printf("\n\t\tMap Phase\t\tReduce Phase\t\tExecution Time\t\tSimulation Error\t\tAvg Percent Diff\n");
    printf("Actual: %17.2f %26.2f %25.2f\n", actual_map, actual_reduce, actual_exec);
    printf("Simulated: %14.2f %26.2f %25.2f %24.2f%% %24.2f%%\n\n", sim_map, sim_reduce, simulation_time, sim_err, avg_percent_diff);
    
    return (res == MSG_OK) ? 0 : 1;
    
}   /* end_of_main */



double get_initialization_cost(msg_host_t h)
{
    double INIT_CALIBRATION_FACTOR = 35;
    return INIT_CALIBRATION_FACTOR * MSG_host_get_speed(h);
}

/*
 * Returns the cost of the map task in flops
 */
double get_map_cost(msg_host_t h)
{
    double flops_per_mb = 13.6;
    return MAP_CALIBRATION_FACTOR * hdfs_chunk_size * flops_per_mb * MSG_host_get_speed(h);
}

double get_bytes_to_shuffle()
{
    return (hdfs_chunk_size_bytes / reducers);
}

double get_reduce_cost(msg_host_t h)
{
    double flops_per_mb = 5.25;
    return REDUCE_CALIBRATION_FACTOR * (input_size / reducers) * flops_per_mb * MSG_host_get_speed(h);
}


void distributeHdfsChunks()
{
    char * key;
    struct HdmsgHost * hdmsg_host;
    xbt_dict_cursor_t cursor = NULL;
    
    long number_of_input_chunks = input_size_bytes / hdfs_chunk_size_bytes;
    
    while (number_of_input_chunks > 0)
    {
        xbt_dict_foreach(hosts, cursor, key, hdmsg_host)
        {
            if (number_of_input_chunks > 0 && hdmsg_host->is_worker)
            {
                add_map_task(hdmsg_host, get_map_cost(hdmsg_host->host));
                number_of_input_chunks--;
            }
        }
    }
    
}

double Log2(double n)
{
    return log(n) / log(2);
}
