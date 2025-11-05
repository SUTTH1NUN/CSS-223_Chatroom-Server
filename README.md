<a id="readme-top"></a>

# CSS-223_Chatroom-Server-

<details>
  <summary>Table of Contents</summary>
  <ol>
    <li><a href="#about-the-project">About The Project</a>
      <ul>
        <li><a href="#built-with">Built With</a></li>
      </ul>
    </li>
    <li><a href="#design-concept">Design Concept</a>
    <li><a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#required">Required</a></li>
        <li><a href="#installation">Installation</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <ul>
        <li><a href="#how-to-test-throungput">How to test throungput</a></li>
      </ul>
    <li><a href="#performance-testing">Performance Testing</a></li>
    <li><a href="#team-member">Team Members</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>

## About the Project
This project is part of the CSS223 Operating System course.
The goal of this project is to develop a Chatroom Server that enables real-time communication between clients using Message Queue and Threaded Router mechanisms.

### Built With
<ul>
  <li>C++</li>
</ul>

## Design Concept
The Chatroom Server is designed based on the Router–Broadcaster architecture, which separates message routing from message delivery to improve concurrency and scalability.
<ul>
  <li>Router Thread — Listens for commands (JOIN, SAY, DM, WHO, LEAVE, QUIT) from the Control Queue, updates registries, and creates broadcast tasks.</li>
  <li>Broadcaster Pool — A group of worker threads that deliver messages to clients through their Reply Queues, allowing non-blocking message handling.</li>
</ul>
The system uses:
<ul>
  <li>Message Queues for inter-process communication.</li>
  <li>Reader–Writer Locks to ensure thread-safe access to the Room Registry and Client Registry.</li>
  <li>Multi-threading to handle multiple clients and broadcasts concurrently.</li>
</ul>
This design demonstrates key OS concepts — IPC, synchronization, and concurrency — while keeping the system simple, modular, and scalable.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Getting Started
Follow these instructions to get a copy of the project up and running on your local machine.

### Required
* Docker
* g++ Version 14.1.0

<p align="right">(<a href="#readme-top">back to top</a>)</p>

### Installation
1. Open terminal.
2. Run the following command to clone this repository in the terminal.
```
git clone https://github.com/SUTTH1NUN/CSS-223_Chatroom-Server.git
```

3. Open folder "CSS-223_Chatroom-Server" in terminal.
```
cd CSS-223_Chatroom-Server
```

4. Run the following commands to build the container using Docker.
```sh
docker build -t myapp:latest .
```
```
docker run -it myapp:latest bash
```

5. Compile the server and client using these commands in the terminal.
```
g++ -std=c++17 ./server/server.cpp -o ./exe/server -lrt -pthread
```
```
g++ -std=c++17 ./client/client.cpp -o ./exe/client -lrt -pthread
```

6. Open another terminal and using this command
```
docker exec -it <docker container ID> bash
```
> To check container ID use "docker ps"

7. Run the commands provided in each terminal as needed.  
If you want to run the server, use:
```
./exe/server
```
If you want to run the client, use:
```
./exe/client
```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Usage
Once the server is running, Each client can connect to the server, set their username, and use the following commands:
1. /list               - Show all Rooms and member counts
2. /create <room>      - Create and join room
3. /join <room>        - Join Room
4. /leave              - Leave current room and return to Lobby
5. /who                - Show users in current room
6. /dm <name> <msg>    - Send Direct Message
7. /members            - Show number of client who online
8. /test               - Run test program
9. /exit               - Disconnect and Quit

<p align="right">(<a href="#readme-top">back to top</a>)</p> 

### How to test throungput

1. Go to Test_Throughtput directory
```
cd Test_Throughtput
```

2. Change file to Unix line endings
```
dos2unix payload.sh
```

3. Run payload.sh
```
bash payload.sh
```

4. When you finish testing and want to run the server or client again, use:
```
cd exe
```
If you want to run the server, use:
```
./exe/server
```
If you want to run the client, use:
```
./exe/client
```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Performance Testing

<p align="center" width="50%"><img width="800" height="500" alt="TestGraph" src="https://github.com/user-attachments/assets/f2977b97-ad92-4f02-af99-167aa4b43bf5" /></p>
<p align="center"><i>A graph showing the relationship between throughput and the number of threads.</i></p>
This graph shows the relationship between server throughput and the number of threads.
From the plot, the throughput is highest when using 1 thread, reaching about 12,125.97 msg/s.
As the number of threads increases (to 2, 4, and 8), the throughput slightly decreases and remains almost constant.
This suggests that increasing the number of threads does not improve performance in this case — and may even cause some overhead or contention that reduces efficiency.


<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Team Member
<ul>
  <li>67090500409 Thanakon Karndee</li>
  <li>67090500420 Sutthinun Sarawiroj</li>
  <li>67090500437 Monpat Thaninthakpong</li>
  <li>67090500441 Sarit Sridit</li>
</ul>

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Acknowledgments
This project was successfully completed thanks to the support and guidance of many people.
We would like to express our sincere gratitude to:
<ul>
  <li>Assoc. Prof. Chukiat Worasucheep, instructor of CSS223 Operating System, for valuable guidance and feedback.</li>
  <li>Our teammates, for their dedication and collaboration throughout the project.></li>
  <li>Online resources and documentation, which helped deepen our understanding of Message Queues, Threads, and Socket Programming.</li>

<p align="right">(<a href="#readme-top">back to top</a>)</p>
