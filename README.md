# pmt

CPU Performance Measurement Tool

pmt is a tool for measuring the relative performance of simple common operations
when run against a specified list of CPUs.


## Installation

pmt is implemented as a loadable kernel module for FreeBSD, and uses sysctl
as its only user interface.

$ git clone git://github.com/beckerg/pmt.git
$ cd pmt
$ make
$ sudo make load


## Running

For example, to get a general idea of the cost of calling atomic_add_long()
in a tight loop on vCPU 0 you could run:

$ sudo sysctl debug.pmt.tests="null empty atomic_add_long"
$ sudo sysctl debug.pmt.run=0x1
$ sysctl debug.pmt.results

Now, run the test again, but this time against two different cores.  Assuming
we have an Intel CPU with at least four cores and HT enabled we'll run the
test on vCPUs 1 and 3:

$ sudo sysctl debug.pmt.run=0xa
$ sysctl debug.pmt.results

As above, but run against two HT vCPUs on the same core, vCPUJS 2 and 3:

$ sudo sysctl debug.pmt.run=0xc
$ sysctl debug.pmt.results
