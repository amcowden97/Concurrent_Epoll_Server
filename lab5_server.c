/**************************************************************************************
Creator: Andrew Michael Cowden
	Email: am.cowden.97@gmail.com
	GitHub Username: amcowden97
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
	"Loki" with the corresponding Pantheon Psuedoterminal.

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
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "readline.c"
#include "tpool.h"

#define MAX_BUFF 4024
#define MAX_TIMER_AMOUNT 2
#define MAX_CLIENTS 100000
#define PORT 4070
#define SECRET "cs407rembash"

//Client State Representation
typedef enum client_state_t {NEW, ESTABLISHED, UNWRITTEN, TERMINATED} Client_State;

//Client Object Representation
typedef struct client_t{
	
	Client_State state;
	int client_fd;
	int master_fd;
	char **unwritten;
	
} Client;

//Instance Variables
int epoll_fd, server_fd;
int bash_pid;
Client **client_pairs;


//Function Prototypes
void timer_sig_handler(int sig);
void handle_epoll();
void handle_bash(char *slave_name);
void accept_clients(int server_fd);
int create_pty_pair(char *slave_name);
int create_socket();
int create_timer(timer_t *p_timer_id);
int verify_protocol(int client_fd);
int transfer_data(int source_fd);
int add_to_epoll(int fd);
void init_client(int client_fd); 
int init_ign_signals();
int init_client_obj(int client_fd);
int start_protocol_exchange(int client_fd);


int main(){
		
	//Setup Signals To Be Ignored
	if(init_ign_signals() == -1){
		perror("In Function (Main), Failure To Set Up Signals To Be Ignored\n\tNOTE"
			   " This Error Terminates The Server Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Create Socket and Bind it with Corresponding Address
	if(create_socket() == -1){
		perror("In Function (Main), Failed To Create Socket And Initialize Socket By"
			   " Calling The Function (create_socket). \n\tNOTE This Error Terminates"
			   " The Server Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Initialize Client Pairs Pointer to Map Struct Addresses
	if((client_pairs = malloc(sizeof(Client*) * MAX_CLIENTS + 5)) == NULL){
		perror("In Function (Main), Failed To Create Array To Hold Client Objects"
			   " Addresses. \n\tNOTE This Error Terminates The Server Program.\n");
		exit(EXIT_FAILURE);
	}
			
	//Make Epoll Unit to Transfer client socket to PTY Master
	if ((epoll_fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
		perror("In Function (Main), Failed To Create An Epoll Unit. This Epoll Thread"
			   " Is The Only Primary Thread In This Program That Does Not Run"
			   " From The Thread Pool \n\tNOTE This Error Terminates The Server"
			   " Program.\n");
		exit(EXIT_FAILURE); 
	}
	
	//Add Server Socket File Descriptor to Epoll Unit
	if(add_to_epoll(server_fd) == -1){
		perror("In Function (main), Failed To Add Server Sock File Descriptor"
			   " To Epoll Unit. \n\tNOTE: This Error Exits The Corresponding Main"
			   " Resulting In The Server Terminating.");
		return -1;
	}
	
	//Create Client Object Memory Allocation Buffer
	
	
	//Call Epoll Loop To Handle Accepting and Handling Clients
	//This Call Will Run Indeffinently Unless an Error Occurs
	handle_epoll(server_fd);
	
	exit(EXIT_FAILURE);
}


void timer_sig_handler(int sig){
	/*Once a timer is set off, it interrupts a blocking read call
	in the thread that called it*/
}


void init_client(int client_fd){
	
	int master_fd;
	char *slave_name;
	
	//Verify Valid Client
	if(verify_protocol(client_fd) == -1){
		perror("In Function (init_client), The Corresponding Client Timed Out Due To"
			   " Incorrect Passphrase.\n\tNOTE: This Error Exits The Corresponding"
			   " Thread Resulting In The Client Terminating.");
		free(&client_pairs[client_fd]);
		close(client_fd);
		return;
	}

	//Open PTY and get Master and Slaves
	if((master_fd = create_pty_pair(slave_name)) == -1){
		perror("In Function (init_client), Failure To Create The PTY Master And"
			   " Slave Pairs.\n\tNOTE: This Error Exits The Corresponding Thread"
			   " Resulting In The Client Terminating.");
		free(&client_pairs[client_fd]);
		close(client_fd);
		return;

	}
	
	//Map Client File Descriptor and Master File Descriptor Pairs
	client_pairs[client_fd]->client_fd = master_fd;
	client_pairs[master_fd] = client_pairs[client_fd];
	
	//Add Master File Descriptor to Epoll Unit
	if(add_to_epoll(master_fd) == -1){
		perror("In Function (init_client), Failed To Add Master File Descriptor"
			   " To Epoll Unit. \n\tNOTE: This Error Exits The Corresponding Thread"
			   " Resulting In The Client Terminating.");
		free(&client_pairs[client_fd]);
		close(master_fd);
		close(client_fd);
		return;
	}
	
	//Handle Bash in Subprocess
	switch((bash_pid = fork())){
		case 0:
			handle_bash(slave_name);
		break;
		case -1:
			perror("In Function (init_client), This Error Results From The Failure"
				   " Of The Fork Call Making A New Process To Run The Client's Bash"
				   " Session. \n\tNOTE: This Error Exits The Corresponding Thread"
				   " Resulting In The Client Terminating.");
			free(&client_pairs[client_fd]);
			close(master_fd);
			close(client_fd);
			return;
	}
}


void handle_epoll(){
	
	int ready, source_fd;
	struct epoll_event evlist[20];

	//Loop and Find FD that are ready for IO
	while ((ready = epoll_wait(epoll_fd, evlist, MAX_CLIENTS * 2, -1)) > 0) {
		for (int i = 0; i < ready; i++) {
	
			//Check if Epoll was Invalid
			if (evlist[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
				close(evlist[i].data.fd);
				close(fd_pairs[evlist[i].data.fd]);
				
			}else if (evlist[i].events & EPOLLIN) {
				//Data is ready to read so transfer:
				source_fd = evlist[i].data.fd;
				tpool_init(selection_operation);
			}
		}
	}
	perror("In Function (handle_epoll - Epoll Loop), \n\tNOTE An Error Occurred"
		   " Causing The Infinite Epoll Loop To Terminate Causing The Server To"
		   " Crash.\n");
}

void select_operation(int source_fd){
	
	//Accept Incoming Client
	if(source_fd == server_fd){  
		accept_clients(source_fd);
		
	//Verify Protocol and Init Client
	}else if(client_pairs[source_fd]->state == NEW){  
		init_client(source_fd);
		
	//Transfer Data Between Client and PTY Master
	}else if(client_pairs[source_fd]->state == ESTABLISHED){  
		transfer_data(source_fd);
	}	
}


void handle_bash(char *slave_name){

	//Create New Session ID
	if(setsid() == -1){
		perror("In Function (handle_bash), Error Setting Session ID In Order To"
			   " Prevent Bash's Need For Seperate Session IDs For Each Process."
			   " \n\tNOTE: This Error Exits The Corresponding Bash Process Resulting"
			   " In The Client Terminating.");
		exit(EXIT_FAILURE);
	}

	//Open PTY Slave
	int slave_fd = open(slave_name, O_RDWR | O_CLOEXEC);
	if(slave_fd == -1){
		perror("In Function (handle_bash), Error Opening Slave File Descriptor For"
			   " PTY. \n\tNOTE: This Error Exits The Corresponding Bash Process"
			   " Resulting In The Client Terminating.");
		exit(EXIT_FAILURE);
	}
	
	free(slave_name);

	//Dup Redirection
	if(dup2(slave_fd, STDOUT_FILENO) == -1 || dup2(slave_fd, STDERR_FILENO) == -1 
	   || dup2(slave_fd, STDIN_FILENO) == -1){	
		
		perror("In Function (handle_bash), Error Redirecting PTY Slave File"
			   " Descriptor To STDIN STDOUT And STDERR For User. \n\tNOTE: This"
			   " Error Exits The Corresponding Bash Process Resulting In The"
			   " Client Terminating.");
		close(slave_fd);
		exit(EXIT_FAILURE);
	}

	//Spawn Bash
	if(execlp("bash", "bash", NULL) == -1){ 
		perror("In Function (handle_bash), Error Execing Bash Process For Client"
			   " Interaction. \n\tNOTE: This Error Exits The Corresponding Bash"
			   " Process Resulting In The Client Terminating.");
		close(slave_fd);
		exit(EXIT_FAILURE);
	}
}


void accept_clients(int accept_fd){
	
	//Variable Declarations for Client Socket Side
	struct sockaddr_in client_address;
    int client_fd;
    socklen_t client_len = sizeof(client_address);
	
	//Server Loop to Accept Clients
    while((client_fd = accept4(accept_fd, (struct sockaddr *) &client_address, &client_len, 
								   SOCK_CLOEXEC | SOCK_NONBLOCK)) > 0){
 
		//Initialize Client Object
		if(init_client_obj(client_fd) == -1){
			perror("In Function (accept_clients), Failed To Initialzie Client Object"
				   " \n\tNOTE: This Error Results In The Client Not Being Accepted.");
			free(&client_pairs[client_fd]);
			close(client_fd);
			continue;
		}
			
		//Add Client File Descriptor to Epoll Unit
		if(add_to_epoll(client_fd) == -1){
			perror("In Function (accept_clients), Failed To Add Client File Descriptor"
				   " To Epoll Unit. \n\tNOTE: This Error Results In The Client Not"
				   " Being Accepted.");
			free(&client_pairs[client_fd]);
			close(client_fd);
			continue;
		}
		
		//Start Protocol Exchange
		if(start_protocol_exchange(client_fd) == -1){
			perror("In Function (accept_clients), Failed To Add Start Protocol Exchange."
				   " \n\tNOTE: This Error Results In The Client Not Being Accepted.");
			free(&client_pairs[client_fd]);
			close(client_fd);
		}
	}
}


int init_client_obj(int client_fd){
	
	//Create Client Object
	Client *client_obj = malloc(sizeof(Client));

	//Set Client State
	client_obj->state = NEW;
	client_obj->client_fd = client_fd;
	client_obj->master_fd = -1;	//Mark For Epoll Unit
	
	//Store Client Object With FD Pairing
	client_pairs[client_fd] = client_obj;
	
	return 0;
}


int start_protocol_exchange(int client_fd){
	
	const char * const rembash_message = "<rembash>\n";
	
	//Rembash Send
	if(write(client_fd, rembash_message, strlen(rembash_message)) < strlen(rembash_message)){
		fputs("Imcomplete Write: Missing Data\n", stderr);
        return -1;
	}
	
	/*//Create Timer to Prevent DOS Attacks
	timer_t timer_id;
	if(create_timer(&timer_id) == -1){
		perror("In Function (verify_protocol), Error Creating POSIX Timer.\n\tNOTE:"
			   " This Error Exits The Corresponding Function");
        return -1;
	}*/
	return 0;
}


int create_pty_pair(char *slave_name){
	
	//Variable to Hold Slave and Master fd and name
	char *slave_temp;
	int master_fd;
	
	//Opent PTY Master File Descriptor
	if((master_fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1){
		perror("In Function (create_pty_master), Error Opening Master File Descriptor"
			   " For The PTY. \n\tNOTE: This Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Set Up Close on Exec and Nonblocking for Master File Descriptor
	if(fcntl(master_fd, F_SETFD, FD_CLOEXEC | O_NONBLOCK) == -1){
		perror("In Function (create_pty_master), Error Setting Up Close On Exec For"
			   " The PTY Master File Descriptor. \n\tNOTE: This Error Exits The"
			   " Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//Unlock Slave PTY File Descriptor
	if(unlockpt(master_fd) == -1){
		perror("In Function (create_pty_master), Error Unlocking The PTY Master File"
			   " Descriptor In Order To Get The PTY Slave Pairing For The PTY."
			   " \n\tNOTE: This Error Exits The Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//Dynamic Memory to Store Slave Name 
	if((slave_name = malloc(MAX_BUFF * sizeof(char))) == NULL){
		perror("In Function (create_pty_master), Error Allocating Memory To Store"
			   " PTY Slave Name To Pass To The Bash Process. \n\tNOTE: This Error"
			   " Exits The Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//Get Slave Name 
	slave_temp = ptsname(master_fd);
	if(slave_temp == NULL){
		perror("In Function (create_pty_master), Error Opening The PTY Slave Name And"
			   " Storing.\n\tNOTE: This Error Exits The Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//See if String is Able to Be Copied
	if(strlen(slave_temp) >= MAX_BUFF){
		perror("In Function (create_pty_master), Error Storing The Slave Name In A"
			   " Temporary Variable To Avoid Writting Over It With Another Function"
			   " Call. \n\tNOTE: This Error Exits The Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//Copy String to New Location
	strncpy(slave_name, slave_temp, MAX_BUFF);
	
	return master_fd;
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
		perror("In Function (create_socket), Failed To Create Server Side"
			  " Of The Connection Socket. \n\tNOTE: This Error Exits The"
			  " Corresponding Function.");
		return -1;
	}
		
	//Bind Address with Socket
    if(bind(server_fd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
		perror("In Function (create_socket), Failed To Bind The Server Socket File"
			   " Descriptor And The Wanted IP Address and Port Number. \n\tNOTE: This"
			   " Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Listen for Connections on Socket
    if(listen(server_fd, MAX_BACKLOG) == -1){
		perror("In Function (create_socket), Failed To Set The Listening Socket As A"
			   " Passive Socket To Accept Incoming Client Connections. \n\tNOTE: This"
			   " Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Set Addresss Reuse in Termination
	int i=1;
	if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) == -1){
		perror("In Function (create_socket), Failed To Set The Address Of The"
			   " Socket To Be Reused In The Event Of Termination To Enhance Testing."
			   " \n\tNOTE: This Error Exits The Corresponding Function.");
		return -1;
	}
	return 0;
}


int create_timer(timer_t *p_timer_id){
	
	//Structs for Timer Siginal Behavior and Timer Length
	struct sigevent sev;
	struct itimerspec time_specs;
	struct sigaction handler_sigaction;
	
	//Set Up Timer Signal Handler
	memset(&handler_sigaction, 0, sizeof(handler_sigaction));
	handler_sigaction.sa_handler = timer_sig_handler;
	sigemptyset(&handler_sigaction.sa_mask);
	sigaction(SIGALRM, &handler_sigaction, NULL);

	//Create POSIX Timer
	sev.sigev_notify = SIGEV_THREAD_ID;
	sev.sigev_signo = SIGALRM;
	sev.sigev_value.sival_ptr = p_timer_id;
	
	//Get Correct Thread ID
	sev._sigev_un._tid = syscall(SYS_gettid);	//Gets thread ID of POSIX Thread
	
	//Create Timer for DOS Attacks
	if(timer_create(CLOCK_REALTIME, &sev, p_timer_id) == -1){
		perror("In Function (create_timer), Failed To Create A POSIX Timer In Order To"
			   " Prevent DOS Attacks. Expiration Of This Timer Results In The"
			   " Disconnection Of The Corresponding Client. \n\tNOTE: This Error Exits"
			   " The Corresponding Function.");
		return -1;
	}
	
	//Set Up Timer Length
	time_specs.it_value.tv_sec = MAX_TIMER_AMOUNT;
	time_specs.it_value.tv_nsec = 0;
	
	//Set Timer Length
	if(timer_settime(*p_timer_id, 0, &time_specs, NULL) == -1){
		perror("In Function (create_timer), Failed To Set The Time Parameters For The"
			   " POSIX Timer. Failure To Set The Timer Parameters Results In"
			   " Disconnection Of The Client.\n\tNOTE: This Error Exits The"
			   " Corresponding Function.");
		return -1;
	}
	return 0;
}


int verify_protocol(int client_fd){
	
	//Variables for Reading and Writing
	char *message_buffer;
	const char * const error_message = "<error>\n";
	const char * const ok_message = "<ok>\n";
	
	//Secret Message Recieving 
	message_buffer = readline(client_fd);
	
	/*//Disarm Timer
	timer_delete(timer_id);*/
	
	//Verify Correct Secret Message
	if(strcmp("<" SECRET ">\n", message_buffer) != 0){
		write(client_fd, error_message, strlen(error_message));
		return -1;
	}

	//Final OK Message
	if(write(client_fd, ok_message, strlen(ok_message)) < strlen(ok_message)){
		fputs("Imcomplete Write: Missing Data\n", stderr);
        return -1;
	}
	
	//Set Client State
	client_pairs[client_fd]->state = ESTABLISHED;
	
	return 0;
}


int transfer_data(int source_fd){
	
	static char *read_buffer; 
	int chars_read = 0;
	int write_fd;
	
	//Determine Writing File Descriptor
	if(source_fd == client_pairs[source_fd]->client_fd){
		write_fd = client_pairs[source_fd]->master_fd;
		
	}else if(source_fd == client_pairs[source_fd]->master_fd){
		write_fd = client_pairs[source_fd]->client_fd;
		
	}else{
		perror("In Function (transfer_data), Failed To Transfer Data"
			   " With The Given File Descriptor\n\tNOTE: This Error Exits The"
			   " Corresponding Function.");
		return -1;
	}
	
	//Memory Allocation Error Checking
	if((read_buffer = malloc(sizeof(char) * MAX_BUFF)) == NULL){
		fputs("Error Allocating Memory\n", stderr);
		return -1;
	}
	
	//Read from File Descriptor
	if((chars_read = read(source_fd, read_buffer, MAX_BUFF)) <= 0){
		fputs("Error reading from File Descriptor\n", stderr);
		return -1;
	}
	
	//Write to File Descriptor
	if((write(write_fd, read_buffer, chars_read)) < chars_read){
		fputs("Did not complete Write System call\n", stderr);
		return -1;
	}
	return 0;
}


int add_to_epoll(int fd){
	
	//Add Client Socket and Master PTY File Descriptors to Epoll
	struct epoll_event ev;
	ev.events = EPOLLIN;
	
  	ev.data.fd = fd;
  	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
    	perror("In Function (add_to_epoll), Error Adding File Descriptor To"
			   " Epoll Unit. \n\tNOTE: This Error Exits The Corresponding Function");
		return -1;
	}
	return 0;
}


int init_ign_signals(){
	
	//Eliminate Need for Child Proccess Collection
	if(signal(SIGCHLD, SIG_IGN) == SIG_ERR){
		perror("In Function (init_ign_signals), Failed To Set Up SIGCHLD Signal To Be Ignored In" 
			   " Order To Disregaurd Collecting Terminated Process Childern. This Call" 
			   " Is Used To Collect the Terminated Bash Subprocess. \n\tNOTE This" 
			   " Error Terminates The Corresponding Function.\n");
		return -1;
	}
	
	//Ignore SIGPIPE Signal To Prevent Clients Crashing Server
	if(signal(SIGPIPE, SIG_IGN) == SIG_ERR){
		perror("In Function (init_ign_signals), Failed To Set Up SIGPIPE Signal To Be Ignored In" 
			   " Order To Prevent Clients From Crashing Server Process On Abnormal Termination" 
			   " \n\tNOTE This Error Terminates The Corresponding Function.\n");
		return -1;
	}
	return 0;
}