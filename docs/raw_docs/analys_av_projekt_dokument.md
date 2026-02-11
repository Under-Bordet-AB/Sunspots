Analys av projektdokument (Tim)
Project Overview

What is the problem?

* Solving the optimization of solar power usage for private homes by forecasting when to use generated energy at once, or when to use energy stored in batteries, when to charge batteries, and when to sell surplus energy from batteries.

How will the system work to solve the problem?

* By combining electricity spot price data with weather forecasting data, the system will determine the optimal timing for when to:
    * use energy generated right away
    * use stored energy
    * charge batteries
    * sell surplus power

What makes up Energy and Weather data-analysis?

* Solar radiation
* Cloud coverage
* Temperature
* Electricity spot prices (15 minutes interval)
* Historical energy usage

What question does the data-analysis seek too answer?

* Optimal time for:
    * direct energy usage
    * stored energy usage
    * energy storage
    * selling surplus energy
    * buying energy

For the coming 24-72 hour period.

What is the output of the system?

* A time-based energy plan for the coming 24-72 hours that show when to:
    * buy power from the grid
    * use own solar energy right away
    * charge the batteries (store energy)
    * discharge the batteries (use energy)
    * sell surplus energy


The following are musts for passing grade

Server solution implemented in C\\

* Multi-threaded design with clear pipeline
    * fetch
    * parse/ transform?
    * compute
    * cash
* Modular design
    * clear responsibilities for each module in the system
    * at least one part of system forked into separate process

Client solution implemented in C or C++\\

* At least one CLI-client
    * C++ client must use RAII och STL when appropriate
    * client must be able to show:
    * energy report from server
    * spot prices

Running and logging

* Handling of API-errors, time-outs or bad data
* System settings control by configuration file
* Controlled starting and stopping of the system
* Logging of all important events

Performance measurements

* Computational steps measured using gprof or perf
* All bottle-necks identified
* Documented optimization with clear "before and after" numbers

The following are stretch-goals

Advanced IPC

* Shared memory with POSIX semaphores for caching
* Message queues for asynchronous communication
* Multiplexed I/O using select() or poll()

Optimization Logic

* Advanced optimization algorithms for energy planning
* Machine-learning–based forecasting
* Battery modeling with charge cycles and degradation

Scalability

* Dynamic thread pool based on system load
* Connection pooling for database/API connections
* Distributed cache with invalidation

Performance

* SIMD optimizations for numerical computations
* Cache-friendly data organization
* Zero-copy I/O for large data transfers

Simulation

* Load simulation for household consumption
* Solar panel modeling with orientation and shading
* Economic simulation with different electricity prices


Deliverables at end of project

Code and System

* Working server and client implementation
* Complete source-code in Git-repo
* Make or other (CMake) build-system
* System configuration files with documentation
* Automated tests

Documentation

* README with installation steps
* Design/ architectural document with flow-charts of system
* Optimization report, before and after
* Individual report from each team-member reflecting over the project

