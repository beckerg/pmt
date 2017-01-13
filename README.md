## pmt

CPU Performance Measurement Tool

pmt is a tool for measuring the relative performance of simple common operations
run against a specified list of CPUs.  It is implemented as a loadable kernel
module for FreeBSD and uses high-priority kernel threads to run tests.  As such,
pmt should never be run on a production machine.

**WARNING** Do not run pmt in a production environment!!!


## Installation

pmt is implemented as a loadable kernel module for FreeBSD and uses sysctl
as its only user interface.

1. $ git clone git://github.com/beckerg/pmt.git
2. $ cd pmt
3. $ make
4. $ sudo make load

#### TSC

By default pmt leverages the time stamp counter (i.e., rdtsc()) to measure
time intervals.  If your machine is not P-state invariant then you need to
compile without PMT_TSC defined (e.g. make -UPMT_TSC).  Otherwise you will
likely get some very erratic and unreproducible results.


## Running

To get a general idea of the cost of calling atomic_add_long() in a tight
loop on vCPU 0 you could run:

1. $ sudo sysctl debug.pmt.tests="null func atomic_add_long"
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


#### Base CLK vs Turbo Mode

Many processors have a turbo frequency at which a core can run under favorable
environmental conditions.  If enabled, pmt is likely to produce inconsistent
results.  To obtain consistent results you can leverage **powerd** to lock
all cores at the base CLK frequency.

The **dev.cpu.0.freq_levels** sysctl should report all the CPU frequency levels
at which the processor can run.  If the processor supports turbo mode then the
first entry will be 1MHz higher than the second.  For example:

1. $ sysctl dev.cpu.0.freq
2. $ sysctl dev.cpu.0.freq_levels
dev.cpu.0.freq_levels: 2801/115000 2800/115000 2700/109145 ...

In this case you'll want to use the second entry (i.e., 2800/115000) ith powerd.
For example, to lock all the processors at the base CLK frequency of 2800:

1. $ sudo service powerd stop
2. $ sudo powerd -m 2800 -M 2800
3. $ sysctl dev.cpu.0.freq


## Tests

pmt comes with a handful of tests (see pmt_tests.c), but it is fairly simple
to add new tests.

* **null** The null test measures the cost of the pmt framework
* **func** The func test measure the cost of calling a function that does nothing
* **inc-shared** The inc-shared test measures the cost of directly incrementing a shared counter (i.e., one counter accessed by all vCPUs with no atomic synchronization)
* **inc-pcpu** The inc-pcpu test measures the cost of directly incrementing a per-cpu counter (i.e., each counter is private to the vCPU).
* **atomic_add_long** The atomic_add_long test measure the cost of directly adding 1 to a shared counter vi the atomic_add_long() function.
* **mutex** The mutex test measures the cost of using a shared mutext to increment a shared counter (i.e., the same mutex and counter are accessed by all vCPUs).
* TODO many others...

While you can run any combination of tests, you generally want to run the **null**
and **func** tests in that order prior to all other tests.  This is because the
**null** test records a baseline of the cost of the pmt framework which is then
subtracted from the **func** test to get a better approximation of the cost
of calling a function.

Subsequently, the **func** test records a baseline which is subtracted from each
ensuing test so as to eliminate the overhead of the framework + function call
from the test in order to get a better approximation of the cost of the body
of the test function.

## Implementation

For each vCPU specified by the debug.pmt.run sysctl, pmt creates a kthread,
affines it to the vCPU, and sets its priority to PRI_MIN_KERN.  At the
PRI_MIN_KERN priority these kthreads will run at a higher priority than
pretty much anything else on the system.  As such, if you run a test againt
all vCPUs in the system the system's responsiveness will be extrememly
sluggish until the test completes.


## Caveats

Currently, overflow can happen in certain situations, typically ones in
which too many vCPUs were used.  If you see a lot of wonky results try
reducing the vCPU count until results become stable.  I will try to fix
this problem soon...

There are likely some situations in which writing to certain pmt sysctls
while a test is in progress can produce erratic results.  I will try to
clean that up, but in the meantime "don't do that!"
