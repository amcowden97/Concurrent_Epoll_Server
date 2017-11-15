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

//Function Prototypes
//void timer_sig_handler(int sig);
int handle_client(int client_fd); 
void handle_bash(char *slave_name);
void *handle_epoll();
int create_pty_pair(char *slave_name);
int create_socket(int *p_server_fd);
//int create_timer(timer_t *p_timer_id);
int verify_protocol(int client_fd);
void transfer_data(int source);
int add_to_epoll(int source_fd);
int init_client_obj(int client_fd);
int accept_clients(int server_fd);

typedef enum {NEW, ESTABLISHED, UNWRITTEN, TERMINATED} Status;

typedef struct client_t{
	
	char *partial_write;
	int client_fd;
	int master_fd;
	Status state;
	
} Client;

//Instance Variables
int epoll_fd, server_fd;
int bash_pid;
int fd_pairs[MAX_CLIENTS * 2 + 5];
Client **client_pairs;




int main(){
		
	//Variable Declarations for Threads
	pthread_t rw_thread;
	
	//Eliminate Need for Child Proccess Collection
	if(signal(SIGCHLD, SIG_IGN) == SIG_ERR){
		perror("\nIn Function (Main), Failed To Set Up SIGCHLD Signal To Be Ignored In" 
			   " Order To Disregaurd Collecting Terminated Process Childern. This Call" 
			   " Is Used To Collect the Terminated Bash Subprocess. NOTE This" 
			   " Error Terminates The Server Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Create Socket and Bind it with Corresponding Address
	if(create_socket(&server_fd) == -1){
		perror("\nIn Function (Main), Failed To Create Socket And Initialize Socket By"
			   " Calling The Function (create_socket). NOTE This Error Terminates"
			   " The Server Program.\n");
		exit(EXIT_FAILURE);
	}
			
	//Make Epoll Unit to Transfer client socket to PTY Master
	if ((epoll_fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
		perror("\nIn Function (Main), Failed To Create An Epoll Unit. This Epoll Thread"
			   " Is The Only Primary Thread In This Program That Does Not Run"
			   " From The Thread Pool. NOTE This Error Terminates The Server"
			   " Program.\n");
		exit(EXIT_FAILURE); 
	}
	
	//Initialize Client Pairs Pointer to Map Struct Addresses
	if((client_pairs = malloc(sizeof(Client*) * MAX_CLIENTS + 5)) == NULL){
		perror("\nIn Function (Main), Failed To Create Array To Hold Client Objects"
			   " Addresses. NOTE This Error Terminates The Server Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Create Thread to Handle File Descriptor Selection and Read/Write
	if(pthread_create(&rw_thread, NULL, handle_epoll, NULL) == -1){
		perror("\nIn Function (Main), Failed To Create POSIX Thread To Handle Epoll"
			   " Unit For File Descriptor Read And Write. NOTE This Error" 
			   " Terminates The Server Program.\n");
		exit(EXIT_FAILURE); 
	}

	//Loop To Accept Clients
	if((accept_clients(server_fd) == -1)){
		perror("\nIn Function (Main). NOTE An Error Occurred Causing The"
			   " Infinite Server Loop To Terminate Causing The Server To Crash.\n");
	}
	exit(EXIT_FAILURE);
}

int accept_clients(int server_fd){
	
	struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);
	int client_fd;
	
	//Infinite Server Loop to Handle Clients
    while(1){
        
		//Accept Client
        if((client_fd = accept4(server_fd, (struct sockaddr *) &client_address, &client_len, SOCK_CLOEXEC)) == -1){
			perror("\nIn Function (Main - Server Loop), Failed To Accept Client Socket"
				   " Address Connection. NOTE This Error Causes The Server"
				   " Loop To Start At the Beginnnig Of Its Execution To Accept"
				   " More Clients.\n");
			continue;
		}
		
		init_client_obj(client_fd);
		
		handle_client(client_fd);
	}
	return -1;
}


int init_client_obj(int client_fd){
	
	//Create Client Object
	if((client_pairs[client_fd] = malloc(sizeof(Client))) == NULL){
		perror("\nIn Function (init_client_obj), Error Allocating Memory To Hold Client"
			   " Structure. NOTE An Error Occurred Causing The Infinite Server"
			   " Loop To Terminate Causing The Server To Crash.\n");
	}

	//Set Client State
	client_pairs[client_fd]->state = NEW;
	client_pairs[client_fd]->client_fd = client_fd;
	client_pairs[client_fd]->master_fd = -1;	//Mark For Epoll Unit
	
	return 0;
}


/*void timer_sig_handler(int sig){
	/*Once a timer is set off, it interrupts a blocking read call
	in the thread that called it
}*/


int handle_client(int client_fd){
	
	int master_fd;
	char *slave_name;
	
	//Verify Valid Client
	if(verify_protocol(client_fd) == -1){
		perror("\nIn Function (handle_client), The Corresponding Client Timed Out Due To"
			   " Incorrect Passphrase. NOTE: This Error Exits The Corresponding"
			   " Thread Resulting In The Client Terminating.");
		close(client_fd);
		return -1;
	}
	
	
	//Dynamic Memory to Store Slave Name 
	if((slave_name = malloc(MAX_BUFF * sizeof(char))) == NULL){
		perror("\nIn Function (handle_client), Error Allocating Memory To Store The"
			   " PTY Slave Name To Pass To The Bash Process. NOTE: This Error"
			   " Exits The Corresponding Thread Resulting In The Client"
			   " Terminating.");
		close(master_fd);
		return -1;
	}

	//Open PTY and get Master and Slaves
	if((master_fd = create_pty_pair(slave_name)) == -1){
			perror("\nIn Function (handle_client), Failure To Create The PTY Master And"
				   " Slave Pairs. NOTE: This Error Exits The Corresponding Thread"
				   " Resulting In The Client Terminating.");
		close(client_fd);
		return -1;

	}
	
	//Add Client Object Mapping With PTY Master
	client_pairs[master_fd] = client_pairs[client_fd];
	
	//Store Client File Descriptor and Master File Descriptor Pairs
	fd_pairs[client_fd] = master_fd;
	fd_pairs[master_fd] = client_fd;
	
	//Add Client File Descriptor to Epoll Unit
	if(add_to_epoll(client_fd) == -1){
		perror("\nIn Function (handle_client), Failed To Add Client File Descriptor" 
			   " To Epoll Unit. NOTE: This Error Exits The Corresponding Thread"
			   " Resulting In The Client Terminating.");
		close(client_fd);
		close(master_fd);
		return -1;
	}
	
	//Add Master File Descriptor to Epoll Unit
	if(add_to_epoll(master_fd) == -1){
		perror("\nIn Function (handle_client), Failed To Add Master File Descriptor" 
			   " To Epoll Unit. NOTE: This Error Exits The Corresponding Thread"
			   " Resulting In The Client Terminating.");
		close(client_fd);
		close(master_fd);
		return -1;
	}
	
	//Handle Bash in Subprocess
	switch((bash_pid = fork())){
		case 0:
			handle_bash(slave_name);
		break;
		case -1:
			perror("\nIn Function (handle_client), This Error Results From The Failure"
				   " Of The Fork Call Making A New Process To Run The Client's Bash"
				   " Session. NOTE: This Error Exits The Corresponding Thread"
				   " Resulting In The Client Terminating.");
			close(client_fd);
			close(master_fd);
	}
	return 0;
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
			perror("\nIn Function (handle_bash), Error Opening Slave File Descriptor For"
				   " PTY. NOTE: This Error Exits The Corresponding Bash Process"
				   " Resulting In The Client Terminating.");
		exit(EXIT_FAILURE);
	}
	
	free(slave_name);

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


void *handle_epoll(){
	
	int ready, source_fd;
	struct epoll_event evlist[20];
	
	//Initialize Thread Pool Function
	tpool_init(transfer_data);

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
				tpool_add_task(source_fd);
			}
		}
	}
	perror("\nIn Function (handle_epoll - Epoll Loop). NOTE An Error Occurred"
		   " Causing The Infinite Epoll Loop To Terminate Causing The Server To"
		   " Crash.\n");
	pthread_exit(NULL);
}


int create_pty_pair(char *slave_name){
	
	//Variable to Hold Slave and Master fd and name
	char *slave_temp;
	int master_fd;
	
	//Opent PTY Master File Descriptor
	if((master_fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1){
		perror("\nIn Function (create_pty_pair), Error Opening Master File Descriptor"
			   " For The PTY. NOTE: This Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Set Up Close on Exec for Master File Descriptor
	if(fcntl(master_fd, F_SETFD, FD_CLOEXEC) == -1){
		perror("\nIn Function (create_pty_pair), Error Setting Up Close On Exec For"
			   " The PTY Master File Descriptor. NOTE: This Error Exits The"
			   " Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//Unlock Slave PTY File Descriptor
	if(unlockpt(master_fd) == -1){
		perror("\nIn Function (create_pty_pair), Error Unlocking The PTY Master File"
			   " Descriptor In Order To Get The PTY Slave Pairing For The PTY."
			   " NOTE: This Error Exits The Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//Get Slave Name 
	slave_temp = ptsname(master_fd);
	if(slave_temp == NULL){
		perror("\nIn Function (create_pty_pair), Error Opening The PTY Slave Name And"
			   " Storing. NOTE: This Error Exits The Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//See if String is Able to Be Copied
	if(strlen(slave_temp) >= MAX_BUFF){
		perror("\nIn Function (create_pty_pair), Error Storing The Slave Name In A"
			   " Temporary Variable To Avoid Writting Over It With Another Function"
			   " Call. NOTE: This Error Exits The Corresponding Function.");
		close(master_fd);
		return -1;
	}
	
	//Copy String to New Location
	strncpy(slave_name, slave_temp, MAX_BUFF);
	
	return master_fd;
}


int create_socket(int *p_server_fd){
	
	//Listening Socket Constant
	const int MAX_BACKLOG = 5;
	
	//Address Initialization
	struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT);
   
	//Socket Initialization
    if((*p_server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1){
			perror("\nIn Function (create_socket), Failed To Create Server Side"
				  " Of The Connection Socket. NOTE: This Error Exits The"
				  " Corresponding Function.");
		return -1;
	}
		
	//Bind Address with Socket
    if(bind(*p_server_fd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
		perror("\nIn Function (create_socket), Failed To Bind The Server Socket File"
			   " Descriptor And The Wanted IP Address and Port Number. NOTE: This"
			   " Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Listen for Connections on Socket
    if(listen(*p_server_fd, MAX_BACKLOG) == -1){
		perror("\nIn Function (create_socket), Failed To Set The Listening Socket As A"
			   " Passive Socket To Accept Incoming Client Connections. NOTE: This"
			   " Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Set Addresss Reuse in Termination
	int i=1;
	if(setsockopt(*p_server_fd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) == -1){
		perror("\nIn Function (create_socket), Failed To Set The Address Of The"
			   " Socket To Be Reused In The Event Of Termination To Enhance Testing."
			   " NOTE: This Error Exits The Corresponding Function.");
		return -1;
	}
	return 0;
}


/*int create_timer(timer_t *p_timer_id){
	
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
}*/


int verify_protocol(int client_fd){
	
	//Variables for Reading and Writing
	char *message_buffer;
	const char * const rembash_message = "<rembash>\n";
	const char * const error_message = "<error>\n";
	const char * const ok_message = "<ok>\n";
	
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
	
	//Mark Client Object as a Valid Client
	client_pairs[client_fd]->state = ESTABLISHED;
	return 0;
}


void transfer_data(int source_fd){
	
	static char *read_buffer; 
	int chars_read = 0;
	
	//Memory Allocation Error Checking
	if((read_buffer = malloc(sizeof(char) * MAX_BUFF)) == NULL){
		fputs("Error Allocating Memory\n", stderr);
		return;
	}
	
	//Check if it is valid
	if(client_pairs[source_fd]->state != ESTABLISHED){
		return;
	}
	
	//Read from File Descriptor
	if((chars_read = read(source_fd, read_buffer, MAX_BUFF)) <= 0){
		fputs("Error reading from File Descriptor\n", stderr);
		return;
	}
	
	//Write to File Descriptor
	if((write(fd_pairs[source_fd], read_buffer, chars_read)) < chars_read){
		fputs("Did not complete Write System call\n", stderr);
		return;
	}
	
	//Reset File Descriptor To Properly Use Epoll's ONESHOT OPTION
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLONESHOT;

	ev.data.fd = source_fd;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, source_fd, &ev) == -1){
		perror("\nIn Function (add_to_epoll), Error Adding Client File Descriptor To"
			   " Epoll Unit. NOTE: This Error Exits The Corresponding Function");
	}
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