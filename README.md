# UNIX Topic-Based Messaging System

## Overview

This project is a robust, topic-based messaging platform developed in C for UNIX-like systems (Linux). It showcases advanced concepts of operating systems, including inter-process communication (IPC), multithreading, and resource synchronization. The system consists of a central `manager` server and multiple `feed` clients, enabling users to subscribe to topics, publish messages, and receive real-time updates in a console-based environment.

This project was developed as a practical assignment for the Operating Systems course at ISEC, demonstrating a deep understanding of low-level system programming and concurrent application design.

## Key Features

* **Topic-Based Communication**: Users can send messages to specific topics (e.g., "sports", "tech").
* **Real-Time Subscriptions**: Subscribers receive messages instantly as they are published to their chosen topics.
* **Persistent & Non-Persistent Messages**: Publishers can set a lifetime for messages. Persistent messages are stored by the `manager` and delivered to new subscribers who join later.
* **Dynamic Topic Creation**: Topics are created automatically when a message is first published to them, provided the system limits are not reached.
* **Concurrent User Support**: The system is designed to handle multiple simultaneous `feed` clients, with each user operating from a different terminal.
* **Centralized Management**: A single `manager` process handles all logic, including user validation, message distribution, and topic management.
* **Administrator Controls**: The `manager` can be controlled via command-line instructions to perform actions like listing active users, removing users, and managing topics (locking/unlocking).
* **Data Persistence**: Persistent messages still within their lifetime are saved to a file when the `manager` shuts down and are reloaded upon restart.

## System Architecture

The platform uses a client-server architecture:

* **Manager (Server)**: The core of the system. It's a single, long-running process responsible for:
    * Authenticating users and ensuring unique usernames.
    * Managing the creation and state (locked/unlocked) of topics.
    * Receiving messages from `feed` clients.
    * Distributing messages to all subscribed clients.
    * Handling the lifecycle of persistent messages.
    * Saving and loading persistent messages from a file defined by the `MSG_FICH` environment variable.

* **Feed (Client)**: The user-facing application. Each user runs their own instance of the `feed` program. Its responsibilities include:
    * Connecting to the `manager` upon startup.
    * Sending user commands (e.g., `subscribe`, `msg`, `exit`) to the `manager`.
    * Listening for incoming messages from subscribed topics and displaying them in the console.
    * Handling user input and system notifications simultaneously.

### Inter-Process Communication (IPC)

Communication between the `manager` and `feed` clients is implemented using **Named Pipes (FIFOs)**. A main pipe (`manager_pipe`) is used by clients to initiate contact, while each `feed` client creates its own unique pipe for receiving dedicated messages and notifications from the `manager`. This ensures a clean and efficient communication channel for each user.

## Technical Stack

* **Language**: C
* **Operating System**: UNIX (Linux)
* **Core Technologies**:
    * **Multithreading**: `pthreads` are used in the `manager` to handle multiple clients and background tasks (like removing expired messages) concurrently.
    * **Synchronization**: `pthread_mutex_t` is employed to protect shared resources (e.g., user and topic lists) from race conditions, ensuring data integrity.
    * **Inter-Process Communication (IPC)**: Named Pipes (FIFOs) are the primary mechanism for communication between the `manager` and `feed` processes.
    * **System Calls**: The implementation prioritizes direct UNIX system calls (`read()`, `write()`, `open()`, `mkfifo()`) over standard library functions for core IPC operations, as per the project requirements.

## Getting Started

### Prerequisites

* A UNIX-like operating system (e.g., Linux).
* `gcc` compiler and `make` utility.

### Compilation

To compile the `manager` and `feed` programs, run the following command in the root directory:

```sh
make all
````

You can also compile the programs individually:

```sh
make manager
make feed
```

### Running the Application

1.  **Set the Environment Variable**: Before starting the manager, define the environment variable `MSG_FICH` which specifies the file for storing persistent messages.

    ```sh
    export MSG_FICH=persistent_messages.txt
    ```

2.  **Start the Manager**: Open a terminal and run the `manager` executable. This process must be running before any client can connect.

    ```sh
    ./manager
    ```

    The manager's terminal will now accept administrator commands (e.g., `users`, `topics`, `lock <topic>`, `close`).

3.  **Start a Feed Client**: Open one or more new terminals to simulate different users. Run the `feed` program, providing a unique username as a command-line argument.

    ```sh
    # In a new terminal
    ./feed pedro

    # In another new terminal
    ./feed maria
    ```

4.  **Interact with the System**: Once connected, you can use commands in the `feed` terminal to interact with the platform.

      * `topics`: List all available topics.
      * `subscribe <topic>`: Subscribe to a topic.
      * `msg <topic> <duration> <message>`: Send a message. `duration` is in seconds (0 for non-persistent).
      * `unsubscribe <topic>`: Unsubscribe from a topic.
      * `exit`: Disconnect and close the feed.

## Cleaning Up

To remove the compiled executables and object files, run:

```sh
make clean
```

```
```
