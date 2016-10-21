import os
import sys
import subprocess

def execute(command):
    process = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = process.communicate()
    process.wait()
    return str(output)

def my_range(start, end, step):
    while start <= end:
        yield start
        start += step

diff = 0.0

actual_map = 0.0
actual_reduce = 0.0

simulated = 0.0

map_cf = 0.0
reduce_cf = 0.0

reducers = 0
input_size = 0
chunk_size = 0

proc = subprocess.Popen("make", shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
proc.wait()

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
print "\nConfiguration: " + config

if config == "512-32-4":
    actual_map = 438
    actual_reduce = 672
elif config == "512-32-8":
    actual_map = 432  
    actual_reduce = 338 
elif config == "512-32-16":
    actual_map = 425
    actual_reduce = 187
elif config == "512-32-4":
    actual_map = 833
    actual_reduce = 664
elif config == "512-32-8":
    actual_map = 821
    actual_reduce = 342
elif config == "512-32-16":
    actual_map = 809
    actual_reduce = 184
elif config == "512-32-4":
    actual_map = 1638
    actual_reduce = 667
elif config == "512-32-8":
    actual_map = 1616
    actual_reduce = 338
elif config == "512-32-16":
    actual_map = 998
    actual_reduce = 183

# Find map_cf
sys.stdout.write('Searching for the best map calibration factor...')
sys.stdout.flush()

diff = sys.float_info.max
for param in my_range(0, 2, 0.01):
    output = execute("./HDMSG " + str(param) + " 1 \n")
    for line in output.split("\\n"):
        if "Simulated" in line:
            simulated = float(line.split()[1])

    if abs(simulated - actual_map) <= diff:
        map_cf = param
        diff = abs(simulated - actual_map)
        #print str(map_cf) + ": " + str(diff) + " " + str(simulated) + " " + str(actual_map)
    else:
        #print str(param) + ": " + str(abs(simulated - actual_map)) + " " + str(simulated) + " " + str(actual_map)
        break

sys.stdout.write(str(map_cf) + "\n")
sys.stdout.flush()

# Find reduce_cf
sys.stdout.write('Searching for the best reduce calibration factor...')
sys.stdout.flush()

diff = sys.float_info.max
for param in my_range(0, 2, 0.01):
    output = execute("./HDMSG " + str(map_cf) + " " + str(param) + " \n")
    for line in output.split("\\n"):
        if "Simulated" in line:
            simulated = float(line.split()[2])

    if abs(simulated - actual_reduce) <= diff:
        reduce_cf = param
        diff = abs(simulated - actual_reduce)
        #print str(reduce_cf) + ": " + str(diff) + " " + str(simulated) + " " + str(actual_reduce)
    else:
        #print str(param) + ": " + str(abs(simulated - actual_reduce)) + " " + str(simulated) + " " + str(actual_reduce)
        break

sys.stdout.write(str(reduce_cf) + "\n\n")
sys.stdout.flush()

output = execute("./HDMSG " + str(map_cf) + " " + str(reduce_cf) + " \n")
output = output.replace("\\n", "\n")
output = output.replace("\\t", "\t")

print output