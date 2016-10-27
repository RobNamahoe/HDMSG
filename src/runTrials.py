import os
import sys
import itertools
import subprocess

def execute(command):
    process = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = process.communicate()
    process.wait()
    return str(output)


reducers = 0
input_size = 0
chunk_size = 0

proc = subprocess.Popen("make", shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
proc.wait()

cfs = [(0.95,1.02)]

configs = [(256,32,4),
           (256,32,8),
           (256,32,16),
           (256,64,4),
           (256,64,8),
           (256,64,16),
           (512,32,4),
           (512,32,8),
           (512,32,16),
           (512,64,4),
           (512,64,8),
           (512,64,16),
           (512,128,4),
           (512,128,8),
           (512,128,16)]

for cf in cfs:
    print '\nCalibration Factors: ' + str(cf[0]) + ', ' + str(cf[1])
    print "\nConfig\t\tMap\t\tReduce\t\tSim_Time\tSim_Err\t\tPercent_Diff"
    
    for conf in configs:
        # Write the config file
        with open('config', 'w') as f:
            f.write('master host0\n')
            f.write('worker host1-host4\n')
            f.write('mappers 0\n')
            f.write('input_size_in_mb ' + str(conf[0]) + '\n')
            f.write('hdfs_chunk_size_in_mb ' + str(conf[1]) + '\n')
            f.write('reducers ' + str(conf[2]) + '\n')

        # Read the config file
        with open('config', 'r') as f:
            for line in f.readlines():
                if "reducers" in line:
                    reducers = int(line.split()[1])
                elif "input_size_in_mb" in line:
                    input_size = int(line.split()[1])
                elif "hdfs_chunk_size_in_mb" in line:
                    chunk_size = int(line.split()[1])

        config =  str(input_size) + "-" + str(chunk_size) + "-" + str(reducers)

        output = execute("./HDMSG " + str(cf[0]) + " " + str(cf[1]) + " config picluster.xml\n")
        for line in output.split("\\n"):
            if "Simulated" in line:
                results = line.split()
                print config + "\t" + str(results[1]) + "\t\t" + str(results[2]) + "\t\t" + str(results[3]) + "\t\t" + str(results[4]) + "\t\t" + str(results[5])


