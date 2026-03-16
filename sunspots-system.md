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
