# Introduction

This document describes the **Sunspots-systems**, its core functionality, and its individual components. Furthermore, it outlines the design philosophy behind the system and its various parts.

Following a general introduction and an overview of the daemon that initializes and maintains the system, the content is organized to follow the system's data flow. This structure provides the reader with a clear understanding of both inputs and outputs simultaneously. Additionally, the headers are categorized by specific domains of responsibility found along the data path.

**Table of Contents**
1. [What is Sunspots-systems](#what-is-sunspots-systems)
   1. [System design](#system-desgin)
2. [Starting and maintaining the system](#starting-and-maintaining-the-system)
   1. [Daemon design choices](#daemon-design-choices)
3. [Fetching data](#fetching-data)
4. [Normalizing data](#normalizing-data)
5. [Storing data](#storing-data)
6. [Retrieving stored data](#retrieving-stored-data)
7. [Computing data and storing results](#computing-data-and-storing-results)
8. [Serving results to clients](#serving-results-to-clients)
9. [Visualizing results for the end user](#visualizing-results-for-the-end-user)

---

## 1 What is Sunspots-systems
A system that helps optimize a user's solar panels to achieve lower energy costs. When operational, Sunspots-systems outputs recommendations on when to:

* Use electricity
* Store electricity
* Sell electricity

These recommendations are provided as a value between 0 and 1. Based on these values, the user can determine the most optimal course of action for their specific needs.

For Sunspots-systems to work as intended, the system is dependent on external inputs. These inputs come in the form of weather forecasts, solar production estimates, and electricity spot-price data.

### 1.1 System design
Sunspots-systems is a distributed system built around the philosophy that each component serves one purpose and one purpose only. Data flows through the system in one direction; none of the components are aware of each other, instead interacting solely by reading from or writing to a common database. 

The daemon relies heavily on the kernel for heavy lifting. Together, they form a modular ecosystem working toward a common goal. The design is general enough that it could be adapted to solve any similar task if the appropriate modules were written.

## 2 Starting and maintaining the system
Sunspots-systems uses a daemon process to start and maintain the modules specified in the configuration file. The responsibility of the daemon is to perform regular health checks of the running programs that comprise the system. This is achieved through system calls to the operating system kernel. Specifically, `inotify`, `signalfd`, and `timerfd` are registered as `epoll` events.

Health checks depend on the timer type configured for the module and come in three different flavors: 
1. **Heartbeats**: RTSIG from the module to the daemon.
2. **Relative time**: e.g., run every 100 seconds.
3. **Absolute time**: e.g., run at 12:00. 

A module with a relative timer can also be configured to run at system boot.

The user can update the configuration file during runtime, which triggers a "hot reload." During this process, the daemon parses the new information and executes the changes immediately. Every module can have a unique config set in the configuration file. That config will be passed through to the module via the shell environment.

The daemon process follows standard conventions for detaching from the shell to ensure it cannot be interrupted. The process runs under the system's `init` process and must be terminated by signaling its PID.

### Daemon design choices
The module is written using the opaque pattern to hide implementation details within the C files. Only public APIs are exposed in the header files; memory allocation and deallocation are handled by the module itself rather than the caller. No global optimization has been performed on the memory layout due to the non-urgent nature of the system; the code utilizes arrays of structs to prioritize ease of use and readability.

## Fetching data
Sunspots-systems fetches upstream data through small source-specific modules that the daemon starts on a schedule. In the current setup, one module retrieves weather forecasts from Open-Meteo using the configured latitude and longitude, while another retrieves electricity spot prices from Elprisetjustnu using the configured Swedish price area.

Each fetcher follows the same general pattern: read runtime settings from the environment, build the correct API URL, perform the HTTP request, parse the returned JSON, and hand the payload over to a transform step. The transformed data is then written into the shared database through the SDK as canonical time-series values, which allows downstream modules to consume one internal format regardless of the original external API.

One technical design choice is that fetching, transformation, and persistence are kept as separate responsibilities. This keeps the source-specific code relatively small and makes it easier to replace an upstream API without changing the compute layer. Another is that fetchers write into the common database rather than talking directly to other modules, which preserves the system's one-way data flow and avoids tight coupling between components.

## Normalizing data
This subsystem parses external JSON data using the cJSON library and converts it into a consistent internal format. It validates required fields, supports both ISO8601 and Unix timestamps, and normalizes values such as temperature into standard units. Where applicable, optional fields are handled gracefully using presence flags, allowing partial data to be processed without failure.

The design is straightforward and explicit, with clear validation and error handling and minimal abstraction beyond a few helper macros. This keeps the code predictable and easy to follow. It is, however, tightly tied to the structure of the upstream JSON, so changes to the API may require updates to the parsing logic.

## Storing data

## Retrieving stored data

## Computing data and storing results
The compute stage reads the canonical weather and price series from the database and turns them into recommendations for the next 24 hours, divided into 96 quarters. Before calculating, the compute manager waits for fresh observed weather data so it does not produce a new plan from stale inputs. It then loads irradiance, cloud cover, temperature, and spot-price values, aligns them into fixed 15-minute slots, and computes the usable horizon from the data that is actually available.

The calculation itself is separated from orchestration. The compute manager selects an algorithm from configuration, currently either a simple heuristic model or a linear-programming-based model, and both produce four normalized control values between 0 and 1: buying electricity, direct use, battery charging, and selling excess energy. This separation makes it possible to experiment with planning strategies without rewriting the surrounding module logic.

Once a result has been produced, the module writes JSON files to the `endpoints` directory. `forecast.json` contains the aligned input forecast data, while `result.json` contains the recommendation series that the frontend and client can serve directly. Storing the output as ready-to-serve JSON is a pragmatic choice: it keeps the serving layer simple and decouples user-facing access from the internal database schema.

## Serving results to clients

## Visualizing results for the end user
The *Sunspots-system* includes a terminal-based client that allows users to view solar optimization recommendations. The client is implemented in C++ with its own TCP/HTTP functionality using POSIX sockets.

Configuration (host, port, and API path) is specified in [src/client/config.hpp](src/client/config.hpp), defaulting to `localhost:10480/endpoints/result.json`.

The client presents an interactive menu with three options:
1. **Live Status** - Displays current quarter values for all four metrics (buy_electricity, charge_battery, sell_excess, direct_use), auto-refreshing every second
2. **Graph** - Renders a color-coded graph plotting 96 quarters (24 hours) ahead
3. **Exit**

When the user selects a viewing option, the client makes an HTTP GET request to fetch the latest data, stores it in `cachedResult`, then displays it according to the selected mode. The client calculates which quarter to display based on the current time and the data's timestamp.
