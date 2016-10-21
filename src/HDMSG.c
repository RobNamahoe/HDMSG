/* Copyright (c) 2010-2014. The SimGrid Team.
 * All rights reserved.                                                     */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

#include "HdmsgHost.h"

#include "msg/msg.h"            /* Yeah! If you want to use msg, you need to include msg/msg.h */
#include "xbt/sysdep.h"         /* calloc, printf */

/* Create a log channel to have nice outputs. */
#include "xbt/log.h"
#include "xbt/asserts.h"

//e_xbt_log_priority_t msgMrCatPriority = xbt_log_priority_critical;
e_xbt_log_priority_t msgMrCatPriority = xbt_log_priority_info;
XBT_LOG_NEW_DEFAULT_CATEGORY(msgMrCat, "Messages specific for this msg application");

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

double avg_map;
double avg_shuffle;
double number_shufflers;
double avg_reduce;

int shuffle_started;

int ready_inits;
int ready_mappers;
int ready_shuffleSenders;
int ready_reducers;

/* Master Process */
int master(int argc, char *argv[])
{
    int ready_processes = 0;
    int ready_shufflers = 0;
    
    msg_task_t task = NULL;
    _XBT_GNUC_UNUSED int res;
    
    char * key;
    struct HdmsgHost *hdmsg_host;
    xbt_dict_cursor_t cursor = NULL;
    
    XBT_INFO("INITIALIZATION BEGIN");
    
    // Initialize processes (mappers, shufflers, and reducers) on each host
    xbt_dict_foreach(hosts, cursor, key, hdmsg_host)
    {
        if (hdmsg_host->is_worker)
        {
            MSG_process_create("Init", initializeProcs, NULL, hdmsg_host->host);
        }
    }

    // Wait for initialization to finish
    ready_processes = 0;
    
    while (1)
    {
        res = MSG_task_receive(&(task), MSG_process_get_name(MSG_process_self()));
        xbt_assert(res == MSG_OK, "MSG_task_get failed: Master (init)");
        
        if (!strcmp(MSG_task_get_name(task), "init process exiting"))
        {
            ready_processes++;
        }
        else
        {
            printf("*** INIT PHASE ERROR Received unexpected task: %s\n", MSG_task_get_name(task));
        }
        
        MSG_task_destroy(task);
        task = NULL;
        
        if (ready_processes == number_of_workers)
        {
            break;
        }
    }
    
    XBT_INFO("INITIALIZATION COMPLETE");
    
    XBT_INFO("MAP PHASE BEGIN");
    
    // Activate Mappers
    xbt_dict_foreach(hosts, cursor, key, hdmsg_host)
    {
        if (hdmsg_host->is_worker)
        {
            activate_mappers(hdmsg_host);
        }
    }
    
    ready_processes = 0;
    
    while (1)
    {
        res = MSG_task_receive(&(task), MSG_process_get_name(MSG_process_self()));
        xbt_assert(res == MSG_OK, "MSG_task_get failed: Master (map)");
        
        if (!strcmp(MSG_task_get_name(task), "shuffle start"))
        {
            XBT_INFO("SHUFFLE PHASE BEGIN");
        }
        else if (!strcmp(MSG_task_get_name(task), "map process exiting"))
        {
            ready_processes++;
        }
        else if (!strcmp(MSG_task_get_name(task), "shuffle send process exiting"))
        {
            ready_shufflers++;
        }
        else
        {
            printf("*** MAP PHASE ERROR Received unexpected task: %s\n", MSG_task_get_name(task));
        }
        
        MSG_task_destroy(task);
        task = NULL;
        
        if (ready_processes == mappers)
        {
            break;
        }
    }
    
    XBT_INFO("MAP PHASE COMPLETE");
    
    // Wait for shuffle phase to end
    
    while (1)
    {
        res = MSG_task_receive(&(task), MSG_process_get_name(MSG_process_self()));
        xbt_assert(res == MSG_OK, "MSG_task_get failed: Master (shuffle)");
        
        if (!strcmp(MSG_task_get_name(task), "shuffle send process exiting"))
        {
            ready_shufflers++;
        }
        else if (!strcmp(MSG_task_get_name(task), "shuffle start"))
        {
            XBT_INFO("SHUFFLE PHASE BEGIN");
        }
        else
        {
            printf("*** SHUFFLE PHASE ERROR Received unexpected task: %s\n", MSG_task_get_name(task));
        }
        
        MSG_task_destroy(task);
        task = NULL;
        
        if (ready_shufflers == reducers * SHUFFLERS_PER_REDUCER)
        {
            break;
        }
    }
    
    XBT_INFO("SHUFFLE PHASE COMPLETE");
    
    XBT_INFO("REDUCE PHASE BEGIN");
    
    // Activate Reducers
    xbt_dict_foreach(hosts, cursor, key, hdmsg_host)
    {
        if (hdmsg_host->is_worker)
        {
            activate_reducers(hdmsg_host);
        }
    }
    
    // Wait for reduce phase to end
    ready_processes = 0;
    
    while (1)
    {
        res = MSG_task_receive(&(task), MSG_process_get_name(MSG_process_self()));
        xbt_assert(res == MSG_OK, "MSG_task_get failed: Master (reduce)");
        
        if (!strcmp(MSG_task_get_name(task), "reduce process exiting"))
        {
            ready_processes++;
        }
        else
        {
            printf("*** REDUCE PHASE ERROR Received unexpected task: %s\n", MSG_task_get_name(task));
        }
        
        MSG_task_destroy(task);
        task = NULL;
        
        if (ready_processes == reducers)
        {
            break;
        }
    }
    
    XBT_INFO("REDUCE PHASE COMPLETE");
    
    return 0;
}                               /* end_of_master */

/** Initialize Processes */
int initializeProcs(int argc, char * argv[])
{
    // Get the current host
    const char * host_name = MSG_host_get_name(MSG_process_get_host(NULL));
    struct HdmsgHost * this_host = xbt_dict_get(hosts, host_name);
    
    int i;
    long mappers_per_worker = MSG_host_get_core_number(this_host->host); //mappers / number_of_workers;
    long reducers_per_worker = reducers / number_of_workers;
    
    // If the number of reducers is not divisible by the number of workers,
    // allocate the remaining reducers
    if (reducers % number_of_workers != 0)
    {
        if (this_host->host_id <= (reducers % number_of_workers))
        {
            reducers_per_worker++;
        }
    }

    // Create mappers
    mappers += mappers_per_worker;
    for (i = 0; i < mappers_per_worker; i++)
    {
        char * mapper_name = bprintf("%s-Mapper-%d", host_name, i);
        msg_process_t mapper = MSG_process_create(mapper_name, map, NULL, this_host->host);
        xbt_fifo_push(this_host->mappers, mapper);
        this_host->active_mappers++;
    }
    
    // Create shufflers
    long number_of_shufflers = SHUFFLERS_PER_REDUCER * reducers_per_worker;
    for (i = 0; i < number_of_shufflers; i++)
    {
        char * sender_name = bprintf("%s-Sender-%d", host_name, i);
        msg_process_t sender = MSG_process_create(sender_name, shuffleSend, NULL, this_host->host);
        xbt_fifo_push(this_host->shuffle_senders, sender);
    }
    
    // Create reducers
    for (i = 0; i < reducers_per_worker; i++)
    {
        char * reducer_name = bprintf("%s-Reducer", host_name);
        msg_process_t reducer = MSG_process_create(reducer_name, reduce, NULL, this_host->host);
        xbt_fifo_push(this_host->reducers, reducer);
    }
    
    // The cost of this task should be equal to the overhead of starting these processes
    MSG_task_execute(MSG_task_create("initialization", get_initialization_cost(this_host->host), 0, NULL));
    
    // Notify master that initialization on this host is complete
    MSG_task_send(MSG_task_create("init process exiting", 0, 1, NULL), "master");

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
            //XBT_INFO("%s is starting a map task", MSG_process_get_name(MSG_process_self()));
            start_time = MSG_get_clock();
            MSG_task_execute(map_task);
            avg_map += MSG_get_clock() - start_time;
            MSG_task_destroy(map_task);
            
            // Partition map output for shufflers to retrieve
            partition_map_task(this_host, bytes_to_shuffle);
        }
    }
    
    this_host->active_mappers--;
    
    // Notify master that I'm done working
    MSG_task_send(MSG_task_create("map process exiting", 0, 1, NULL), "master");
    
    return 0;
}


/** Shuffle Send Process */
int shuffleSend(int argc, char * argv[])
{
    double start_time;
    
    msg_task_t task = NULL;
    _XBT_GNUC_UNUSED int res;
    
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
                MSG_task_dsend(MSG_task_create("shuffle start", 0, 1, NULL), "master", NULL);
            }
            
            // Create a shuffle receiver on the recipient host
            msg_host_t recipient_host = MSG_task_get_data(task);
            char * receiver_name = bprintf("%s->%s-Receiver", process_name, MSG_host_get_name(recipient_host));
            MSG_process_create(receiver_name, shuffleReceive, NULL, recipient_host);
            
            // Send the task to the shuffle receiver
            start_time = MSG_get_clock();
            MSG_task_send(task, receiver_name);
            avg_shuffle += MSG_get_clock() - start_time;
            number_shufflers++;
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
    
    MSG_task_send(MSG_task_create("shuffle send process exiting", 0, 1, NULL), "master");
    
    return 0;
}


/** Shuffle Receive Process */
int shuffleReceive(int argc, char * argv[])
{
    msg_task_t task = NULL;
    _XBT_GNUC_UNUSED int res;
    
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
    
    start_time = MSG_get_clock();
    MSG_task_execute(MSG_task_create("reduce", get_reduce_cost(MSG_host_self()), 0, NULL));
    avg_reduce += MSG_get_clock() - start_time;

    MSG_task_send(MSG_task_create("reduce process exiting", 0, 1, NULL), "master");
    
    return 0;
}

/** Main function */
int main(int argc, char *argv[])
{
    int i, BYTES_PER_MEGABYTE = 1048576;
    
    msg_error_t res = MSG_OK;
    
    MSG_init(&argc, argv);
    
    if (argc != 3)
    {
        printf("Usage: %s map_cf reduce_cf\n", argv[0]);
        printf("Example: %s 0.28 0.29\n", argv[0]);
        exit(1);
    }
    
    // Set calibration factors
    sscanf(argv[1], "%lf", &MAP_CALIBRATION_FACTOR);
    sscanf(argv[2], "%lf", &REDUCE_CALIBRATION_FACTOR);

    // Register the functions
    MSG_function_register("master", master);
    MSG_function_register("initializeProcs", initializeProcs);
    
    MSG_function_register("map", map);
    MSG_function_register("reduce", reduce);
    
    MSG_function_register("shuffleSend", shuffleSend);
    MSG_function_register("shuffleReceive", shuffleReceive);

    // Read config file and set parameters
    FILE * config_file = fopen("config", "r");
    
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
                mappers = atoi(value);
            }
            else if (strcmp(key, "reducers") == 0)
            {
                if (isdigit(*value))
                {
                    reducers = atoi(value);
                }
                else
                {
                    // evaluate expression
                    
                }
            }
            else if (strcmp(key, "input_size_in_mb") == 0)
            {
                input_size = atol(value);
                input_size_bytes = input_size * BYTES_PER_MEGABYTE;
            }
            else if (strcmp(key, "hdfs_chunk_size_in_mb") == 0)
            {
                hdfs_chunk_size = atoi(value);
                hdfs_chunk_size_bytes = hdfs_chunk_size * BYTES_PER_MEGABYTE;
            }
            else if (strcmp(key, "platform") == 0)
            {
                MSG_create_environment(value);
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
    
    avg_map /= input_size_bytes / hdfs_chunk_size_bytes;;
    avg_reduce /= reducers;
    
    int iX, iY, iZ;
    iX = log2(input_size) - 8;
    iY = log2(hdfs_chunk_size) - 5;
    iZ = log2(reducers) - 2;
    
    // If I don't have actual execution times, then don't print stats just exit.
    if (iX >= 3 || iY >= 3 || iZ >= 3) { return (res == MSG_OK) ? 0 : 1; }
    
    double mapTimes[3][3][3];  // Input size (256, 512, 1024), Chunk size (32, 64, 128), Number of reducers (4, 8, 16)
    
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
    double sum_of_diffs = fabs(avg_map - actual_map) + fabs(avg_reduce - actual_reduce);
    
    double actual_exec = actualTimes[iX][iY][iZ];
    double sim_err = (fabs(simulation_time - actual_exec) / actual_exec) * 100;
    
    // Write results to file
    
    FILE * output_file = NULL;
    output_file = fopen("HDMSG_output.txt", "a");
    
    fprintf(output_file, "%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
            sum_of_diffs,
            sim_err,
            MAP_CALIBRATION_FACTOR,
            REDUCE_CALIBRATION_FACTOR,
            avg_map,
            actual_map,
            avg_reduce,
            actual_reduce,
            simulation_time,
            actual_exec);
            
    fclose(output_file);
    
    // Write results to the console
    printf("\n\t\tMap Phase\t\tReduce Phase\t\tExecution Time\t\tSimulation Error\t\tSum of Diffs\n");
    printf("Actual: %17.2f %26.2f %25.2f\n", actual_map, actual_reduce, actual_exec);
    printf("Simulated: %14.2f %26.2f %25.2f %24.2f%% %24.2f\n\n", avg_map, avg_reduce, simulation_time, sim_err, sum_of_diffs);
    
    return (res == MSG_OK) ? 0 : 1;
    
}   /* end_of_main */



double get_initialization_cost(msg_host_t h)
{
    double INIT_CALIBRATION_FACTOR = 30;
    return INIT_CALIBRATION_FACTOR * MSG_get_host_speed(h);
}

/*
 * Returns the cost of the map task in flops
 */
double get_map_cost(msg_host_t h)
{
    double flops_per_mb = 13.6;
    return MAP_CALIBRATION_FACTOR * hdfs_chunk_size * flops_per_mb * MSG_get_host_speed(h);
}

double get_bytes_to_shuffle()
{
    return (hdfs_chunk_size_bytes / reducers);
}

double get_reduce_cost(msg_host_t h)
{
    double flops_per_mb = 5.25;
    return REDUCE_CALIBRATION_FACTOR * (input_size / reducers) * flops_per_mb * MSG_get_host_speed(h);
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
