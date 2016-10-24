import os
import sys
import time
import itertools
import subprocess

class ExpResult:
    def __init__(self, data):
        values = data.split()
        self.map_cf = values[0]
        self.reduce_cf = values[1]
        self.sim_map = values[2]
        self.actual_map = values[3]
        self.sim_redu = values[4]
        self.actual_redu = values[5]
        self.sim_elap = values[6]
        self.actual_elap = values[7]
        self.sim_err = float(values[8])
        self.sum_of_diffs = float(values[9]) # cast these to floats since they are used to sort the list
        self.diff_sum_err = float(values[10])

        self.cfs = self.map_cf + ' , ' + self.reduce_cf
        self.map_stats = self.sim_map + ' , ' + self.actual_map
        self.reduce_stats = self.sim_redu + ' , ' + self.actual_redu
        self.exe_stats = self.sim_elap + ' , ' + self.actual_elap
        self.stats = [self.cfs, self.map_stats, self.reduce_stats, self.exe_stats, self.sum_of_diffs]

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

filename = "Ranked by Sum of Diff.txt"
if os.path.isfile(filename):
    os.remove(filename)

fout = open(filename, 'a')
fout.write("Exhaustive search results ordered by lowest SUM OF DIFFERENCES AS PERCENTAGE OF MEASURED EXECUTION TIME (sum_of_diffs / actual_exec)\n\n");
fout.write("sum_of_diff(%)      sim_err(%)     map_cf, reduce_cf     sim_map , actual_map    sim_redu , actual_redu      sim_elap , actual_elap  sum_of_diff\n");

for r in sorted(results, key=lambda x: x.diff_sum_err):
    new_stats = list(r.stats)
    new_stats.insert(0, r.sim_err)
    new_stats.insert(0, r.diff_sum_err)
    fout.write('{:>8} {:>17} {:>21} {:>24} {:>25} {:>28} {:>10}\n'.format(*new_stats))
    del new_stats[:]

fout.close()


print "Ranking by the Simulation Error..."

filename = "Ranked by Simulation Error.txt"
if os.path.isfile(filename):
    os.remove(filename)

fout = open(filename, 'a')
fout.write("Exhaustive search results ordered by SIMULATION ERROR (simulation_time - actual_exec) / actual_exec)\n\n");
fout.write("sim_err(%)      sum_of_diff(%)         map_cf, reduce_cf     sim_map , actual_map    sim_redu , actual_redu      sim_elap , actual_elap  sum_of_diff\n");

for r in sorted(results, key=lambda x: x.sim_err):
    new_stats = list(r.stats)
    new_stats.insert(0, r.diff_sum_err)
    new_stats.insert(0, r.sim_err)
    fout.write('{:>7} {:>15} {:>29} {:>24} {:>24} {:>28} {:>11}\n'.format(*new_stats))
    del new_stats[:]

fout.close()

print "Done"
