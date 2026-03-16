# Introduction

This document describes the **Sunspots-systems**, its core functionality, and its individual components. Furthermore, it outlines the design philosophy behind the system and its various parts.

Following a general introduction and an overview of the daemon that initializes and maintains the system, the content is organized to follow the system's data flow. This structure provides the reader with a clear understanding of both inputs and outputs simultaneously. Additionally, the headers are categorized by specific domains of responsibility found along the data path.

**Table of Contents**
1. [What is Sunspots-systems](#what-is-sunspots-systems)
   1. [System design](#system-desgin)
2. [Starting and maintaining the system](#starting-and-maintaining-the-system)
3. [Fetching data](#fetching-data)
4. [Normalizing data](#normalizing-data)
5. [Storing data](#storing-data)
6. [Retrieving stored data](#retrieving-stored-data)
7. [Computing data and storing results](#computing-data-and-storing-results)
8. [Serving results to clients](#serving-results-to-clients)
9. [Visualizing results for the end user](#visualizing-results-for-the-end-user)

---

## 1 What is Sunspots-systems
A system that helps optimize the users solar panels towards lower energy costs. When operational *Sunspots-systems* outputs recommendations on when to:
- use electricity 
- store electricity 
- sell electricity

These recommendations are given as a value between *0* and *1*. Based on these values the user can  determine the most optimal course of action for his/ hers needs.

For *Sunspots-systems* to work as intended for this purpose the system is depentend on input. These inputs comes in the form of **weather, solar and electricity spotprice data**. 

### 1.1 System design
*Sunspots-systems* is a distributed system formed around the idea that each part of the system serves one purpose and one purpose only. Data flows through the system one way only and none if the components know of each other, instead they all either read from or write to a common database. The daemon relies heavily on the kernel for all the heavy lifting. Together they form a system of parts that work together that work towards a common goal. The system is general enough that it could be fitted to solve any similar task if the right modules were written. 

## Starting and maintaining the system

## Fetching data

## Normalizing data

## Storing data

## Retrieving stored data

## Computing data and storing results

## Serving results to clients

## Visualizing results for the end user
The *Sunspots-system* includes a terminal-based client that allows users to view solar optimization recommendations. The client is implemented in C++ with its own TCP/HTTP functionality using POSIX sockets.

Configuration (host, port, and API path) is specified in [src/client/config.hpp](src/client/config.hpp), defaulting to `localhost:10480/endpoints/result.json`.

The client presents an interactive menu with three options:
1. **Live Status** - Displays current quarter values for all four metrics (buy_electricity, charge_battery, sell_excess, direct_use), auto-refreshing every second
2. **Graph** - Renders a color-coded graph plotting 96 quarters (24 hours) ahead
3. **Exit**

When the user selects a viewing option, the client makes an HTTP GET request to fetch the latest data, stores it in `cachedResult`, then displays it according to the selected mode. The client calculates which quarter to display based on the current time and the data's timestamp.
