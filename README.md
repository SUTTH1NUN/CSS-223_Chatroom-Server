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


## Getting Started
Follow these instructions to get a copy of the project up and running on your local machine.

### Required
* Docker
* g++ Version 14.1.0

### Installation
1. Download this repository.
```sh
git clone https://github.com/SUTTH1NUN/CSS-223_Chatroom-Server.git
```
2. Open folder "CSS-223_Chatroom-Server"
3. Run the following commands. docker compose.yaml
```sh
cd docker
...
```
4. Complie server and client ... to exe
```Server
g++ -std=c++17 server.cpp -o server -lrt -pthread
```
```Client
g++ -std=c++17 client.cpp -o client -lrt -pthread
```
```sh
./server
./client
```
<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Usage
Once the server is running, clients can connect and use the following commands:
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

## Performance Testing

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
  <li>Assoc. Prof. Chookiat Warasuchip, instructor of CSS223 Operating System, for valuable guidance and feedback.</li>
  <li>Our teammates, for their dedication and collaboration throughout the project.></li>
  <li>Online resources and documentation, which helped deepen our understanding of Message Queues, Threads, and Socket Programming.</li>

<p align="right">(<a href="#readme-top">back to top</a>)</p>
