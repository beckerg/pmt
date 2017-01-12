# pmt

CPU Performance Measurement Tool

pmt is a tool for measuring the relative performance of simple common operations
when run against a specified list of CPUs.


## Installation

pmt is implemented as a loadable kernel module for FreeBSD, and uses sysctl
as its only user interface.

1. $ git clone git://github.com/beckerg/pmt.git
2. $ cd pmt
3. $ make
4. $ sudo make load


## Running

For example, to get a general idea of the cost of calling atomic_add_long()
in a tight loop on vCPU 0 you could run:

1. $ sudo sysctl debug.pmt.tests="null empty atomic_add_long"
2. $ sudo sysctl debug.pmt.run=0x1
3. $ sysctl debug.pmt.results

Now, run the test again, but this time against two different cores.  Assuming
we have an Intel CPU with at least four cores and HT enabled we'll run the
test on vCPUs 1 and 3:

1. $ sudo sysctl debug.pmt.run=0xa
2. $ sysctl debug.pmt.results

As above, but run against two HT vCPUs on the same core, vCPUJS 2 and 3:

1. $ sudo sysctl debug.pmt.run=0xc
2. $ sysctl debug.pmt.results
