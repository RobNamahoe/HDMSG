import os
import sys
import time
import itertools
import subprocess

class ExpResult:
    def __init__(self, data):
        values = data.split()
        self.sum_of_diffs = float(values[0])
        self.sim_err = float(values[1])
        self.map_cf = values[2]
        self.reduce_cf = values[3]
        self.sim_map = values[4]
        self.actual_map = values[5]
        self.sim_redu = values[6]
        self.actual_redu = values[7]
        self.sim_elap = values[8]
        self.actual_elap = values[9]

def my_range(start, end, step):
    while start <= end:
        yield start
        start += step

def execute(command):
    process = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = process.communicate()
    exitCode = process.returncode

if os.path.isfile('HDMSG_output.txt'):
    os.remove('HDMSG_output.txt')

with open('HDMSG_output.txt', "w") as f:
    f.write("sum_of_diffs, sim_err, map_cf, reduce_cf, sim_map, actual_map, sim_redu, actual_redu, sim_elap, actual_elap\n");

if os.path.isfile('ranked_output.txt'):
    os.remove('ranked_output.txt')

execute("make")

maps = []
maps.extend(my_range(0.8, 1.2, 0.01))

reduce = []
reduce.extend(my_range(0.8, 1.2, 0.01))

params = [maps, reduce]
products = list(itertools.product(*params))
length = len(products)

print '\nExecuting ' + str(length) + ' combinations'

sys.stdout.write('Progress: 0% ')
sys.stdout.flush()

count = 0.0
procs = []
fnull = open(os.devnull, 'w')

i = 0
progress = 10

for product in products:

    count += 1.0
    
    if length >= 100:
        if count % (length / 10) == 0:
            sys.stdout.write(str(progress) + '% ')
            sys.stdout.flush()
            progress += 10
    
    command = "./HDMSG " + str(product[0]) + " " + str(product[1]) + "\n"
    p = subprocess.Popen(command, shell=True, stdout=fnull, stderr=fnull)
    procs.append(p)

    if i == 10:
        i = 0
        for proc in procs:
            proc.wait()
            procs.remove(proc)
    else:
        i += 1

for proc in procs:
    proc.wait()
    procs.remove(proc)


results = []
with open('HDMSG_output.txt', 'r') as f:
    for line in f:
        command = line.rstrip()
        if not "map_cf" in command:
            results.append(ExpResult(command))


print "\nRanking by the Sum of Differences..."

filename = "Ranked by Sum of differences.txt"
if os.path.isfile(filename):
    os.remove(filename)

fout = open(filename, 'a')
fout.write("Exhaustive search results ordered by lowest SUM OF DIFFERENCES\n\n");
fout.write("sum_of_diff: map_cf, reduce_cf     sim_map , actual_map    sim_redu , actual_redu      sim_elap , actual_elap      sim_err\n");

for r in sorted(results, key=lambda x: x.sum_of_diffs):
    fout.write(str(r.sum_of_diffs) + "\t\t" + r.map_cf + " , " + r.reduce_cf + "\t\t" + r.sim_map + " , " + r.actual_map + "\t\t" + r.sim_redu + " , " + r.actual_redu + "\t\t" + r.sim_elap + " , " + r.actual_elap + "\t\t" +  str(r.sim_err) + "\n")

fout.close()


print "Ranking by the Simulation Error..."

filename = "Ranked by Simulation Error.txt"
if os.path.isfile(filename):
    os.remove(filename)

fout = open(filename, 'a')
fout.write("Exhaustive search results ordered by SIMULATION ERROR\n\n");
fout.write("sim_err: map_cf, reduce_cf     sim_map , actual_map    sim_redu , actual_redu      sim_elap , actual_elap      sum_of_diffs\n");

for r in sorted(results, key=lambda x: x.sim_err):
        fout.write(str(r.sim_err) + "\t\t" + r.map_cf + " , " + r.reduce_cf + "\t\t" + r.sim_map + " , " + r.actual_map + "\t\t" + r.sim_redu + " , " + r.actual_redu + "\t\t" + r.sim_elap + " , " + r.actual_elap + "\t\t" +  str(r.sum_of_diffs) + "\n")

fout.close()

print "Done"
