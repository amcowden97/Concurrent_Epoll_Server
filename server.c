/**************************************************************************************
Creator: Andrew Michael Cowden
	Email: am.cowden.97@gmail.com
	Github Username: amcowden97
Date Created: October 15, 2017

Project Name: Concurrent Epoll Linux Server
Project Description:
	The functionality of this project allows for a number of remote clients to access
	the files found on the server machine. It is quite similar to a Telnet
	implementation as it does not use encryption techniques. This project uses a
	multithreaded approach with Linux's Epoll in order to handle clients efficiently
	and in a greater number than the traditional multiprocess approach. In order to
	increase the response time and to avoid client blocking, a thread pool is used to
	handle the client threads as well as the inital verification process for clients.
	
Use and Development Notes:
	The client and server programs developed on for use on Linux Systems only due to
	Linux specific structures. These programs have been tested on Elementary OS 0.4.1
	"Loki" with the corresponding Pantheon PseudoTerminal.
	
A Note About Dyanmic Memory Allocation:
	Within this program, several instances of dynamic memory allocation are used to
	transfer data or client objects. This current version does not free this allocated
	memory but rather uses its own memory handling through a preallocated list of
	memory.
**************************************************************************************/

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include <stdio.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include "readline.c"
#include "tpool.h"

#define MAX_BUFF 4024
#define MAX_TIMER_AMOUNT 5
#define MAX_CLIENTS 100000
#define REARM_IN 0
#define REARM_OUT 1
#define NOT_MARK 0
#define MARK 1
#define PORT 4070
#define SECRET "cs407rembash"

//Function Prototypes
void handle_epoll();
void dispatch_operation(int source_fd);
void accept_clients(int server_fd);
void handle_timers();
void verify_protocol(int client_fd);
void transfer_data(int source);
void unwritten_data(int source_fd);
void handle_bash(char *slave_name);
void terminate_client(int client_fd, int master_fd, int mark_terminated);
void *allocate_client_memory(size_t size);

int create_socket();
int create_pty_pair(int client_fd, char *slave_name);
int init_client(int client_fd); 
int send_protocol(int client_fd);
int add_to_epoll(int source_fd);
int rearm_epoll(int source_fd, int in_or_out);
int init_client_obj(int client_fd);
int create_timer();

typedef enum {NEW, ESTABLISHED, UNWRITTEN, TERMINATED} Status;

typedef struct client_t{
	
	char *unwritten;
	int unwritten_num;
	int client_fd;
	int master_fd;
	Status state;
	
} Client;

typedef struct linked_list_t{
	
	Client* data;
	struct linked_list_t *next;
	
} Linked_Memory;


//Instance Variables
int epoll_fd, timer_epoll_fd, server_fd;
int bash_pid;
int fd_pairs[MAX_CLIENTS * 2 + 5];
int clock_pairs[MAX_CLIENTS * 2 + 5];
Client **client_pairs;


int main(){
			
	//Eliminate Need for Child Proccess Collection
	if(signal(SIGCHLD, SIG_IGN) == SIG_ERR){
		perror("\nIn Function (Main), Failed To Set Up SIGCHLD Signal To Be Ignored In" 
			   " Order To Disregaurd Collecting Terminated Process Childern. This Call" 
			   " Is Used To Collect the Terminated Bash Subprocess. NOTE: This" 
			   " Error Terminates The Server Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Create Socket and Bind it with Corresponding Address
	if(create_socket() == -1){
		perror("\nIn Function (Main), Failed To Create Socket And Initialize Socket By"
			   " Calling The Function (create_socket). NOTE: This Error Terminates"
			   " The Server Program.\n");
		exit(EXIT_FAILURE);
	}
			
	//Make Epoll Unit to Transfer Data Between Clients And Server
	if ((epoll_fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
		perror("\nIn Function (Main), Failed To Create An Epoll Unit. This Epoll Thread"
			   " Is The Only Primary Thread In This Program That Does Not Run"
			   " From The Thread Pool. NOTE: This Error Terminates The Server"
			   " Program.\n");
		exit(EXIT_FAILURE); 
	}
	
	//Make Epoll Unit to Monitor Timers
	if ((timer_epoll_fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
		perror("\nIn Function (Main), Failed To Create An Epoll Unit. This Epoll Unit Is"
			   " Used For Client Timers. NOTE: This Error Terminates The Server"
			   " Program.\n");
		exit(EXIT_FAILURE); 
	}
	
	//Initialize Client Pairs Pointer to Map Struct Addresses
	if((client_pairs = malloc(sizeof(Client*) * MAX_CLIENTS + 5)) == NULL){
		perror("\nIn Function (Main), Failed To Create Array To Hold Client Objects"
			   " Addresses. NOTE: This Error Terminates The Server Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Add Epoll Timer File Descriptor to Epoll Unit
	if(add_to_epoll(timer_epoll_fd) == -1){
		perror("\nIn Function (main), Failed To Add Timer Epoll File Descriptor" 
			   " To Epoll Unit. NOTE: This Error Results In The Server Terminating.");
		exit(EXIT_FAILURE);
	}
	
	//Add Server File Descriptor to Epoll Unit
	if(add_to_epoll(server_fd) == -1){
		perror("\nIn Function (main), Failed To Add Server File Descriptor" 
			   " To Epoll Unit. NOTE: This Error Results In The Server Terminating.");
		exit(EXIT_FAILURE);
	}
	
	//Call Function To Handle Epoll Selection
	handle_epoll();
	
	exit(EXIT_FAILURE);
}


void handle_epoll(){
	
	int ready, source_fd;
	struct epoll_event evlist[20];
	
	//Initialize Thread Pool Function
	tpool_init(dispatch_operation);

	//Loop and Find FD that are ready for IO
	while ((ready = epoll_wait(epoll_fd, evlist, MAX_CLIENTS * 2, -1)) > 0){
		for (int i = 0; i < ready; i++) {
	
			//Check if Epoll was Invalid
			if (evlist[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)){
				terminate_client(evlist[i].data.fd, fd_pairs[evlist[i].data.fd], MARK);
				
			}else if ((evlist[i].events & EPOLLIN) || (evlist[i].events & EPOLLOUT)){
				//Data is ready to read so transfer:
				source_fd = evlist[i].data.fd;
				tpool_add_task(source_fd);
			}
		}
	}
	perror("\nIn Function (handle_epoll - Epoll Loop). NOTE An Error Occurred"
		   " Causing The Infinite Epoll Loop To Terminate Causing The Server To"
		   " Crash.\n");
}


void dispatch_operation(int source_fd){
		
	if(source_fd == server_fd){ //Accept Clients State
		accept_clients(source_fd);
		
	}else if(source_fd == timer_epoll_fd){
		handle_timers();
		
	}else{
		switch(client_pairs[source_fd]->state){ //Client Object Case	
			case NEW:
				verify_protocol(source_fd);
				break;
				
			case ESTABLISHED:
				transfer_data(source_fd);
				break;
				
			case UNWRITTEN:
				unwritten_data(source_fd);
				break;
				
			case TERMINATED:
				perror("Terminated Case...........................");
				break;
				
			default:
				perror("Something");
		}
	}
}


void accept_clients(int server_fd){
	
	struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);
	int client_fd;
	
	//Server Loop to Accept Clients
    while((client_fd = accept4(server_fd, (struct sockaddr *) &client_address,
							   &client_len, SOCK_CLOEXEC | SOCK_NONBLOCK)) > 0){
		
		//Initialize Client Struct
		if(init_client_obj(client_fd) == -1){
			perror("\nIn Function (accept_clients), Error Initializing Client"
				   " Struct. NOTE: This Ends The Client Connection.\n");
			terminate_client(client_fd, -1, NOT_MARK);
			return;
		}
		
		//Add Client File Descriptor to Epoll Unit
		if(add_to_epoll(client_fd) == -1){
			perror("\nIn Function (accept_clients), Failed To Add Client File Descriptor" 
				   " To Epoll Unit. NOTE: This Error Results In The Client Terminating.");
			terminate_client(client_fd, -1, MARK);
			return;
		}
		
		//Send First Part Of Protocol Verification
		if(send_protocol(client_fd) == -1){
			perror("\nIn Function (accept_clients), Error Sending Rembash"
				   " Protocol. NOTE: This Ends The Client Connection.\n");
			terminate_client(client_fd, -1, MARK);
			return;
		}
		
		//Rearm Epoll For Input
		if(rearm_epoll(server_fd, REARM_IN) == -1){
			perror("\nIn Function (accept_clients), Error Rearming Server File"
				   " Descriptor For Epoll Loop. NOTE: This Terminates The Client"
				   " Connection.\n");
			terminate_client(client_fd, -1, MARK);
		}
	}
}


void handle_timers(int timer_epoll_fd){
	
	int ready, source_fd;
	struct epoll_event evlist[20];
	
	//Loop and Find FD that are ready for IO
	if((ready = epoll_wait(timer_epoll_fd, evlist, MAX_CLIENTS * 2, -1)) > 0){
		for (int i = 0; i < ready; i++) {
			
			perror("Timer Expired");
			source_fd = evlist[i].data.fd;
			terminate_client(clock_pairs[source_fd], -1, MARK);
			close(source_fd);
		}
	}
	
	//Rearm Epoll For Input
	if(rearm_epoll(timer_epoll_fd, REARM_IN) == -1){
		perror("\nIn Function (handle_timers), Error Rearming Epoll Timer File"
			   " Descriptor For Epoll Loop. NOTE: This Terminates The Client"
			   " Connection.\n");
	}
}


void verify_protocol(int client_fd){
	
	//Variables for Reading and Writing
	char *message_buffer;
	const char * const error_message = "<error>\n";
	const char * const ok_message = "<ok>\n";
	
	//Secret Message Recieving 
	message_buffer = readline(client_fd);
	
	//Verify Correct Secret Message
	if(strcmp("<" SECRET ">\n", message_buffer) != 0){
		write(client_fd, error_message, strlen(error_message));
		perror("\nIn Function (verify_protocol), Incorrect Secret Message." 
			   " NOTE: This Error Closes The Client.\n");
		terminate_client(client_fd, -1, MARK);
		return;
	}
	
	//Disarm File Descriptor Timer
	close(clock_pairs[client_fd]);

	//Final OK Message
	if(write(client_fd, ok_message, strlen(ok_message)) < strlen(ok_message)){
		perror("\nIn Function (verify_protocol), Error Sending OK Message"
			   " Message To Client. NOTE This Error Causes The Client To Terminate.\n");
		terminate_client(client_fd, -1, MARK);
		return;
	}
	
	//Initialize Client
	if(init_client(client_fd) == -1){
		perror("\nIn Function (verify_protocol), Error Initalizing Client With PTY And"
			   " Bash Subprocess. NOTE: This Error Closes The Client.");
		return; //Client Termination Handled In init_client Function
	}
	
	//Rearm Epoll For Input
	if(rearm_epoll(client_fd, REARM_IN) == -1){
		perror("\nIn Function (verify_protocol), Error Rearming Client File"
			   " Descriptor For Epoll Loop. NOTE: This Terminates The Client"
			   " Connection.\n");
		terminate_client(client_fd, -1, MARK);
		return;
	}
	
	//Mark Client Object as a Valid Client
	client_pairs[client_fd]->state = ESTABLISHED;
}


void transfer_data(int source_fd){
	
	static char read_buffer[MAX_BUFF]; 
	int chars_read = 0, chars_written = 0;
		
	//Read from File Descriptor
	if((chars_read = read(source_fd, read_buffer, MAX_BUFF)) < 0){
		perror("\nIn Function (transfer_data), Error Reading... NOTE: This Error Exits"
			   " The Corresponding Function");
		terminate_client(source_fd, fd_pairs[source_fd], MARK);
		return;
	}
	
	//Write to File Descriptor
	if((chars_written = write(fd_pairs[source_fd], read_buffer, chars_read))
	   < chars_read){
		
		//Partial Write Case	
		if(errno == EAGAIN){
			client_pairs[source_fd]->unwritten = (read_buffer + chars_written);
			client_pairs[source_fd]->unwritten_num = chars_read - chars_written;
			client_pairs[source_fd]->state = UNWRITTEN;
			
			if(rearm_epoll(source_fd, REARM_OUT) == -1){
				perror("\nIn Function (transfer_data), Error Rearming Source File Descriptor For"
					   " Epoll Unit. NOTE: This Error Terminates The Client Connection.\n");
				terminate_client(source_fd, fd_pairs[source_fd], MARK);
				return;
			}
			
		//True Error Writing Case
		}else{								
			perror("\nIn Function (transfer_data), Error Writing... NOTE: This Error"
				   " Exits The Corresponding Function");
			
			if(rearm_epoll(source_fd, REARM_OUT) == -1){
				perror("\nIn Function (unwritten_data), Error Rearming Source File Descriptor For Epoll"
					  "Unit. NOTE: This Error Terminates The Client Connection.\n");
				terminate_client(source_fd, fd_pairs[source_fd], MARK);
				return;
			}
			
			terminate_client(source_fd, fd_pairs[source_fd], MARK);
			return;
		}
	//Default Successful Write Case
	}else{
		if(rearm_epoll(source_fd, REARM_IN) == -1){
				perror("\nIn Function (transfer_data), Error Rearming Source File Descriptor For"
					   " Epoll Unit. NOTE: This Error Terminates The Client Connection.\n");
				terminate_client(source_fd, fd_pairs[source_fd], MARK);
				return;
		}
	}
}


void unwritten_data(int source_fd){
	
	int chars_read = client_pairs[source_fd]->unwritten_num, chars_written = 0;
	
	//Write to File Descriptor
	if((chars_written = write(fd_pairs[source_fd],  client_pairs[source_fd]->unwritten,
							  chars_read)) < chars_read){
		
		//Partial Write Error	
		if(errno != EAGAIN){
			perror("\nIn Function (unwritten_data), Error Writing... NOTE: This Error"
				   " Exits The Corresponding Function");
			
			terminate_client(source_fd, fd_pairs[source_fd], MARK);
			return;
		
		//Partial Write Again Case
		}else{								
			
			//perror("Partial Write AGain...........");
			client_pairs[source_fd]->unwritten = (client_pairs[source_fd]->unwritten + chars_written);
			client_pairs[source_fd]->unwritten_num = chars_read - chars_written;			
			client_pairs[source_fd]->state = UNWRITTEN;
			
			if(rearm_epoll(source_fd, REARM_OUT) == -1){
				perror("\nIn Function (unwritten_data), Error Rearming Source File Descriptor For Epoll"
					  "Unit. NOTE: This Error Terminates The Client Connection.\n");
				terminate_client(source_fd, fd_pairs[source_fd], MARK);
				return;
			}
		}
		
	//Successful Write Case
	}else{
		client_pairs[source_fd]->state = ESTABLISHED;
		
		if(rearm_epoll(source_fd, REARM_IN) == -1){
				perror("\nIn Function (unwritten_data), Error Rearming Source File Descriptor For Epoll"
					  "Unit. NOTE: This Error Terminates The Client Connection.\n");
				terminate_client(source_fd, fd_pairs[source_fd], MARK);
				return;
		}
	}
}


void handle_bash(char *slave_name){

	//Create New Session ID
	if(setsid() == -1){
		perror("\nIn Function (handle_bash), Error Setting Session ID In Order To"
			   " Prevent Bash's Need For Seperate Session IDs For Each Process."
			   " NOTE: This Error Exits The Corresponding Bash Process Resulting"
			   " In The Client Terminating.");
		exit(EXIT_FAILURE);
	}

	//Open PTY Slave
	int slave_fd = open(slave_name, O_RDWR | O_CLOEXEC);
	if(slave_fd == -1){
			perror("\nIn Function (handle_bash), Error Opening Slave File Descriptor For PTY. NOTE: This Error Exits The Corresponding Bash Process"
				   " Resulting In The Client Terminating.");
		exit(EXIT_FAILURE);
	}
	
	//Dup Redirection
	if(dup2(slave_fd, STDOUT_FILENO) == -1 || dup2(slave_fd, STDERR_FILENO) == -1 || dup2(slave_fd, STDIN_FILENO) == -1){	
			perror("\nIn Function (handle_bash), Error Redirecting PTY Slave File"
				   " Descriptor To STDIN STDOUT And STDERR For User. NOTE: This"
				   " Error Exits The Corresponding Bash Process Resulting In The"
				   " Client Terminating.");
		close(slave_fd);
		exit(EXIT_FAILURE);
	}

	//Spawn Bash
	if(execlp("bash", "bash", NULL) == -1){ 
			perror("\nIn Function (handle_bash), Error Execing Bash Process For Client"
				   " Interaction. NOTE: This Error Exits The Corresponding Bash"
				   " Process Resulting In The Client Terminating.");
		close(slave_fd);
		exit(EXIT_FAILURE);
	}
}


void terminate_client(int client_fd, int master_fd, int mark_terminated){
	
	//Mark Client Object Terminated
	if(mark_terminated){
		client_pairs[client_fd]->state = TERMINATED;
	}else{
		close(client_fd);	//Failrue In Accept Clients Before Obj Allocation
		return;
	}
	
	//Close Corresponding File Descriptors
	if(master_fd == -1){
		close(client_fd);
		free(client_pairs[client_fd]);
	}else{
		close(client_fd);
		close(master_fd);
		free(client_pairs[client_fd]);
	}
}


int create_socket(){
	
	//Listening Socket Constant
	const int MAX_BACKLOG = 5;
	
	//Address Initialization
	struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT);
   
	//Socket Initialization
    if((server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) == -1){
			perror("\nIn Function (create_socket), Failed To Create Server Side"
				  " Of The Connection Socket. NOTE: This Error Exits The"
				  " Corresponding Function.");
		return -1;
	}
		
	//Bind Address with Socket
    if(bind(server_fd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
		perror("\nIn Function (create_socket), Failed To Bind The Server Socket File"
			   " Descriptor And The Wanted IP Address and Port Number. NOTE: This"
			   " Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Listen for Connections on Socket
    if(listen(server_fd, MAX_BACKLOG) == -1){
		perror("\nIn Function (create_socket), Failed To Set The Listening Socket As A"
			   " Passive Socket To Accept Incoming Client Connections. NOTE: This"
			   " Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Set Addresss Reuse in Termination
	int i=1;
	if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) == -1){
		perror("\nIn Function (create_socket), Failed To Set The Address Of The"
			   " Socket To Be Reused In The Event Of Termination To Enhance Testing."
			   " NOTE: This Error Exits The Corresponding Function.");
		return -1;
	}
	return 0;
}


int create_pty_pair(int client_fd, char *slave_name){
	
	//Variable to Hold Slave and Master fd and name
	char *slave_temp;
	int master_fd;
	
	//Opent PTY Master File Descriptor
	if((master_fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1){
		perror("\nIn Function (create_pty_pair), Error Opening Master File Descriptor"
			   " For The PTY. NOTE: This Error Exits The Corresponding Function.");
		terminate_client(client_fd, -1, MARK);
		return -1;
	}
	
	//Set Up Close on Exec for Master File Descriptor
	if(fcntl(master_fd, F_SETFD, FD_CLOEXEC | O_NONBLOCK) == -1){
		perror("\nIn Function (create_pty_pair), Error Setting Up Close On Exec For"
			   " The PTY Master File Descriptor. NOTE: This Error Exits The"
			   " Corresponding Function.");
		terminate_client(client_fd, master_fd, MARK);
		return -1;
	}
	
	//Unlock Slave PTY File Descriptor
	if(unlockpt(master_fd) == -1){
		perror("\nIn Function (create_pty_pair), Error Unlocking The PTY Master File"
			   " Descriptor In Order To Get The PTY Slave Pairing For The PTY."
			   " NOTE: This Error Exits The Corresponding Function.");
		terminate_client(client_fd, master_fd, MARK);
		return -1;
	}
	
	//Get Slave Name 
	slave_temp = ptsname(master_fd);
	if(slave_temp == NULL){
		perror("\nIn Function (create_pty_pair), Error Opening The PTY Slave Name And"
			   " Storing. NOTE: This Error Exits The Corresponding Function.");
		terminate_client(client_fd, master_fd, MARK);
		return -1;
	}
	
	//See if String is Able to Be Copied
	if(strlen(slave_temp) >= MAX_BUFF){
		perror("\nIn Function (create_pty_pair), Error Storing The Slave Name In A"
			   " Temporary Variable To Avoid Writting Over It With Another Function"
			   " Call. NOTE: This Error Exits The Corresponding Function.");
		terminate_client(client_fd, master_fd, MARK);
		return -1;
	}
	
	//Copy String to New Location
	strncpy(slave_name, slave_temp, MAX_BUFF);
	
	return master_fd;
}


int init_client(int client_fd){
	
	int master_fd;
	char slave_name[MAX_BUFF];
	
	//Open PTY and get Master and Slaves
	if((master_fd = create_pty_pair(client_fd, slave_name)) == -1){
			perror("\nIn Function (init_client), Failure To Create The PTY Master And"
				   " Slave Pairs. NOTE: This Error Exits The Corresponding Thread"
				   " Resulting In The Client Terminating.");
		return -1; //Client Termination Occurs In Function create_pty_pair
	}
	
	//Add Client Object Mapping With PTY Master
	client_pairs[master_fd] = client_pairs[client_fd];
	
	//Store Client File Descriptor and Master File Descriptor Pairs
	fd_pairs[client_fd] = master_fd;
	fd_pairs[master_fd] = client_fd;
	
	//Add Master File Descriptor to Epoll Unit
	if(add_to_epoll(master_fd) == -1){
		perror("\nIn Function (init_client), Failed To Add Master File Descriptor" 
			   " To Epoll Unit. NOTE: This Error Exits The Corresponding Thread"
			   " Resulting In The Client Terminating.");
		terminate_client(client_fd, master_fd, MARK);
		return -1;
	}
	
	//Handle Bash in Subprocess
	switch((bash_pid = fork())){
		case 0:
			//Close File Descriptors For Error Handling In handle_bash Function
			close(client_fd);
			close(master_fd);
			handle_bash(slave_name);
		break;
		case -1:
			perror("\nIn Function (init_client), This Error Results From The Failure"
				   " Of The Fork Call Making A New Process To Run The Client's Bash"
				   " Session. NOTE: This Error Exits The Corresponding Thread"
				   " Resulting In The Client Terminating.");
			terminate_client(client_fd, master_fd, MARK);
			return -1;
	}
	return 0;
}


int init_client_obj(int client_fd){
	
	//Create Client Object
	if((client_pairs[client_fd] = malloc(sizeof(Client))) == NULL){
		perror("\nIn Function (init_client_obj), Error Allocating Memory To Hold"
			   " Client Structure. NOTE An Error Occurred Causing The Infinite Server"
			   " Loop To Terminate Causing The Server To Crash.\n");
		return -1;
	}

	//Set Client State
	client_pairs[client_fd]->state = NEW;
	client_pairs[client_fd]->client_fd = client_fd;
	return 0;
}


int send_protocol(int client_fd){
	
	const char * const rembash_message = "<rembash>\n";
	
	//Rembash Send
	if(write(client_fd, rembash_message, strlen(rembash_message)) < strlen(rembash_message)){
		perror("\nIn Function (send_protocol), Error Sending Rembash Protocol Message"
			   " To Client. NOTE This Error Causes The Client To Terminate.\n");
        return -1;
	}
	
	//Create Timer to Prevent DOS Attacks
	int timer_fd;
	if((timer_fd = create_timer()) == -1){
		perror("In Function (verify_protocol), Error Creating POSIX Timer.\n\tNOTE:"
			   " This Error Exits The Corresponding Function");
        return -1;
	}
	
	//Add Timer File Descriptor To Epoll Timer
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLONESHOT;
	
  	ev.data.fd = timer_fd;
  	if(epoll_ctl(timer_epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev) == -1){
    	perror("\nIn Function (send_protocol), Error Adding Timer File Descriptor To"
			   " Timer Epoll Unit. NOTE: This Error Exits The Corresponding Function");
		return -1;
	}
	
	//Map Client And Timer FDs
	clock_pairs[client_fd] = timer_fd;
	clock_pairs[timer_fd] = client_fd;
	
	return 0;
}


int add_to_epoll(int source_fd){
	
	//Add Client Socket and Master PTY File Descriptors to Epoll
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLONESHOT;
	
  	ev.data.fd = source_fd;
  	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, source_fd, &ev) == -1){
    	perror("\nIn Function (add_to_epoll), Error Adding Source File Descriptor To"
			   " Epoll Unit. NOTE: This Error Exits The Corresponding Function");
		return -1;
	}
	return 0;
}


int rearm_epoll(int source_fd, int in_or_out){

	struct epoll_event ev;
	ev.data.fd = source_fd;
	
	//Choose EPOLLIN or EPOLLOUT
	switch(in_or_out){
		case REARM_IN:
			ev.events = EPOLLIN | EPOLLONESHOT;
			break;
		case REARM_OUT:
			ev.events = EPOLLOUT | EPOLLONESHOT;
	}

	//Reset File Descriptor To Properly Use Epoll's ONESHOT OPTION
	if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, source_fd, &ev) == -1){
		perror("\nIn Function (rearm_epoll), Error Rearming Source File"
			   " Descriptor For EPOLLONESHOT. NOTE: This Error Exits The Corresponding"
			   " Function");
		return -1;
	}
	return 0;
}


int create_timer(){
	
	struct itimerspec time_specs;
	int timer_fd;
	
	//Create Timer for DOS Attacks
	if((timer_fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC)) == -1){
		perror("In Function (create_timer), Failed To Create FD Timer In Order To"
			   " Prevent DOS Attacks. Expiration Of This Timer Results In The"
			   " Disconnection Of The Corresponding Client. \n\tNOTE: This Error Exits"
			   " The Corresponding Function.");
		return -1;
	}
	
	//Set Up Timer Length
	time_specs.it_interval.tv_sec = 0;
	time_specs.it_interval.tv_nsec = 0;
	time_specs.it_value.tv_sec = MAX_TIMER_AMOUNT;
	time_specs.it_value.tv_nsec = 0;
	
	//Set Timer Length
	if(timerfd_settime(timer_fd, 0, &time_specs, NULL) == -1){
		perror("In Function (create_timer), Failed To Set The Time Parameters For The"
			   " FD Timer. Failure To Set The Timer Parameters Results In"
			   " Disconnection Of The Client.\n\tNOTE: This Error Exits The"
			   " Corresponding Function.");
		return -1;
	}
	return timer_fd;
}