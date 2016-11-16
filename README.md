# HDMSG
Overview
--------
A simulation of Apache's Hadoop, an open-source implementation of Google's MapReduce framework.

Requirements
------------
[SimGrid v3.13](http://simgrid.gforge.inria.fr)

Build Instructions
------------------
1. Edit the Makefile to include the path to your SimGrid installation.
2. Run the 'make' command from the command line.

Running HDMSG
-------------
The simulation accepts four command line arguments:<br>
1. Map Calibration Factor <br>
2. Reduce Calibration Factor <br>
3. MapReduce job configuration file <br>
4. SimGrid platform file <br>

Calibration factors are necessary to recoup the computation and communication costs lost due to the simulation being an abstraction of the target system.
The MapReduce job configuration file defines the master and worker nodes, number of Mapper and Reducer processes, input file size, and the block size of the simulated distributed file system.
The platform file describes the system on which the application is executed. The syntax is defined by SimGrid.
