# Concurrent_Epoll_Server

## Project Description
This repository holds the basic form of a Linux client and server pair using Epoll and thread pooling to add concurrent support. The functionality of the client/server pairing is to allow command line access from a remote machine onto the server. It acts as an ssh implementation without encryption support. The initial concept stems from the design of the Nginx web server allowing for decreased waiting and server response time. I found the article written by Andrew Alexeev regarding Nginx a great source to understand the fundemental connections required in development. I have provided a link as follows: [nginx by Andrew Alexeev](http://www.aosabook.org/en/nginx.html). 

The development of this project was not to create a replacement of other developed software but rather a way for me to improve my design and development skills as well as gain a better understanding of larger scaled projects. This client/server pairing does keep security of software in mind but has not undergone rigorous penetration testing. Please keep this in mind when experimenting with this application. 

## Project Usage
Using this software requires access to a machine that runs a distribution of the Linux Operating System. Note: This application was developed and tested on Linux Mint and Elementary OS Operating Systems. The current design of this application can be executed as follows:

 Note: Please alter the secret message preprocessor constants found in both the client and server code before use to provide additional security. 
  

## Development Overview
* Socket Connection

   The initial implementation stage focused on the socket connection between the client and server to provide basic functionality. By providing the desired server ip when executing the client side, it will preform the 3-way handshake set in place. After the client in verified, your desired bash terminal can remotely interact with the server machine.
   
  
* Terminals and Psuedoterminals
* Thread Pool
* File Descriptor Timers
* Partial Writes

## Future Additions and Applications

## Contact
