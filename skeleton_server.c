/***********************************************************************************************
Creator: Andrew Michael Cowden
	Email: am.cowden.97@gmail.com
	Github Username: amcowden97
Date Created: October 15, 2017

Project Name: Concurrent Epoll Linux Server
Project Description:
	The functionality of this project allows for a number of remote clients to access
	the files found on the server machine. It is quite similar to a Telnet implementation
	as it does not use encryption techniques. This project uses a multithreaded approach with 
	Linux's Epoll in order to handle clients efficiently and in a greater number than the 
	traditional multiprocess approach. In order to increase the response time and to avoid 
	client blocking, a thread pool is used to handle the client threads as well as the inital 
	verification process for clients. 
	
Use and Development Notes:
	The client and server programs developed on for use on Linux Systems only due to Linux
	specific structures. These programs have been tested on Elementary OS 0.4.1 "Loki" with
	the corresponding Pantheon PseudoTerminal.
	
A Note About Dyanmic Memory Allocation:
	Within this program, several instances of dynamic memory allocation are used to thread data
	or client objects. This current version does not free this allocated memory but rather uses
	its own memory handling through a preallocated list of memory.
************************************************************************************************/

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

#define MAX_BUFF 4024
#define MAX_TIMER_AMOUNT 2
#define MAX_CLIENTS 100000
#define PORT 4070
#define SECRET "cs407rembash"

//Epoll Instance Variables
int epoll_fd;
int fd_pairs[MAX_CLIENTS * 2 + 5];

//Bash Instance Variable
int bash_pid;

//Function Prototypes
void *handle_client(void *arg); 
void handle_bash(char *slave_name);
void *epoll_select();
void timer_sig_handler(int sig);
int verify_protocol(int connect_fd);
int transfer_data(int read_fd, int write_fd);
int create_pty_master(char *slave_name);
int create_socket(int *p_server_sockfd);
int create_timer(timer_t *p_timer_id);


int main(){
		
	//Variable Declarations for Socket Ends (Client and Server)
	struct sockaddr_in client_address;
    int server_sockfd, client_sockfd;
    socklen_t client_len;
	
	//Variable Declarations for Threads
	pthread_t rw_thread, rembash_thread;
	
	//Eliminate Need for Child Proccess Collection
	if(signal(SIGCHLD, SIG_IGN) == SIG_ERR){
		perror("In Function (Main), Failed To Set Up SIGCHLD Signal To Be Ignored In" 
			   " Order To Disregaurd Collecting Terminated Process Childern. This Call" 
			   " Is Used To Collect the Terminated Bash Subprocess. \n\tNOTE This" 
			   " Error Terminates The Server Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Create Socket and Bind it with Corresponding Address
	if(create_socket(&server_sockfd) == -1){
		perror("In Function (Main), Failed To Create Socket And Initialize Socket By"
			   " Calling The Function (create_socket). \n\tNOTE This Error Terminates"
			   " The Server Program.\n");
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
	
	//Create Thread to Handle File Descriptor Selection and Read/Write
	if(pthread_create(&rw_thread, NULL, epoll_select, NULL) == -1){
		perror("In Function (Main), Failed To Create POSIX Thread To Handle Epoll"
			   " Unit For File Descriptor Read And Write. \n\tNOTE This Error" 
			   " Terminates The Server Program.\n");
		exit(EXIT_FAILURE); 
	}

	//Infinite Server Loop to Handle Clients
    while(1){
        
		//Accept Client
        client_len = sizeof(client_address);
        if((client_sockfd = accept4(server_sockfd, (struct sockaddr *) &client_address, &client_len, SOCK_CLOEXEC)) == -1){
			perror("In Function (Main - Server Loop), Failed To Accept Client Socket"
				   " Address Connection. \n\tNOTE This Error Causes The Server"
				   " Loop To Start At the Beginnnig Of Its Execution To Accept"
				   " More Clients.\n");
			continue;
		}
		
		//Save Client File Descriptor for Thread Process
		int *fdptr;
		if((fdptr = malloc(sizeof(int))) == NULL){
			perror("In Function (Main - Server Loop), Failed To Allocate Memory To"
				   " Send Client File Descriptor To Rembash Validation. \n\tNOTE"
				   " This Error Closes The Corresponding Client File Descriptor and"
				   " Causes The Server Loop To Start At the Beginnnig Of Its Execution"
				   " To Accept More Clients.\n");
			close(client_sockfd);
			continue;
		}
		
		*fdptr = client_sockfd;
		
		//Create Thread for Verifying Rembash Protocol
		if(pthread_create(&rembash_thread, NULL, handle_client, fdptr) == -1){
        		perror("In Function (Main - Server Loop), Failed To Create POSIX"
					   " Thread To Verify Client.\n\tNOTE This Error Closes The"
					   " Corresponding Client File Descriptor and Causes The Server"
					   " Loop To Start At the Beginnnig Of Its Execution To Accept"
					   " More Clients.\n");
			close(client_sockfd);
			continue;
		}
	}
	exit(EXIT_SUCCESS);
}


int create_socket(int *p_server_sockfd){
	
	//Listening Socket Constant
	const int MAX_BACKLOG = 5;
	
	//Address Initialization
	struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT);
   
	//Socket Initialization
    if((*p_server_sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1){
			perror("In Function (create_socket), Failed To Create Server Side"
				  " Of The Connection Socket. \n\tNOTE: This Error Exits The"
				  " Corresponding Function.");
		return -1;
	}
		
	//Bind Address with Socket
    if(bind(*p_server_sockfd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
		perror("In Function (create_socket), Failed To Bind The Server Socket File"
			   " Descriptor And The Wanted IP Address and Port Number. \n\tNOTE: This"
			   " Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Listen for Connections on Socket
    if(listen(*p_server_sockfd, MAX_BACKLOG) == -1){
		perror("In Function (create_socket), Failed To Set The Listening Socket As A"
			   " Passive Socket To Accept Incoming Client Connections. \n\tNOTE: This"
			   " Error Exits The Corresponding Function.");
		return -1;
	}
	
	//Set Addresss Reuse in Termination
	int i=1;
	if(setsockopt(*p_server_sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) == -1){
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
		fputs("Error Creating Timer\n", stderr);
		return -1;
	}
	
	//Set Up Timer Length
	time_specs.it_value.tv_sec = MAX_TIMER_AMOUNT;
	time_specs.it_value.tv_nsec = 0;
	
	//Set Timer Length
	if(timer_settime(*p_timer_id, 0, &time_specs, NULL) == -1){
		fputs("Error Setting the Time for the POSIX Timer\n", stderr);
		return -1;
	}
	return 0;
}


void *handle_client(void *arg){
	
	int master_fd, connect_fd;
	char *slave_name;
	
	//Get Client File Descriptor
	connect_fd = *(int*)arg;
	free(arg);
	
	//Verify Valid Client
	if(verify_protocol(connect_fd) == -1){
		fputs("Unable to Connect Client\n", stderr);
		close(connect_fd);
		pthread_exit(NULL);
	}

	//Dynamic Memory to Store Slave Name 
	if((slave_name = malloc(MAX_BUFF * sizeof(char))) == NULL){
		fputs("Error Allocating Memeory\n", stderr);
		close(connect_fd);
		pthread_exit(NULL);
	}

	//Open PTY and get Master and Slaves
	if((master_fd = create_pty_master(slave_name)) == -1){
		fputs("Error Opening Master File Descriptor\n", stderr);
		close(connect_fd);
		pthread_exit(NULL);

	}
	
	//Add Client Socket and Master PTY File Descriptors to Epoll
	struct epoll_event ev;
	ev.events = EPOLLIN;
	
  	ev.data.fd = connect_fd;
  	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connect_fd, &ev) == -1){
    	fputs("Error Adding Client Socket to Epoll Event Queue\n", stderr);
		close(connect_fd);
		close(master_fd);
		pthread_exit(NULL);
	}
	
	ev.data.fd = master_fd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, master_fd, &ev) == -1){
    	fputs("Error Adding Master File Descriptor to Epoll Event Queue", stderr);
		close(connect_fd);
		close(master_fd);
		pthread_exit(NULL);
	}

	//Store Client File Descriptor and Master File Descriptor Pairs
	fd_pairs[connect_fd] = master_fd;
	fd_pairs[master_fd] = connect_fd;
	
	//Handle Bash in Subprocess
	switch((bash_pid = fork())){
		case 0:
			handle_bash(slave_name);
		break;
		case -1:
			fputs("Error Forking to Handle Bash Process\n", stderr);
			close(connect_fd);
	}
	pthread_exit(NULL);
}


int verify_protocol(int connect_fd){
	
	//Variables for Reading and Writing
	char *message_buffer;
	const char * const rembash_message = "<rembash>\n";
	const char * const error_message = "<error>\n";
	const char * const ok_message = "<ok>\n";
	
	//Rembash Send
	if(write(connect_fd, rembash_message, strlen(rembash_message)) < strlen(rembash_message)){
		fputs("Imcomplete Write: Missing Data\n", stderr);
		close(connect_fd);
        return -1;
	}
	
	//Create Timer to Prevent DOS Attacks
	timer_t timer_id;
	if(create_timer(&timer_id) == -1){
		fputs("Timer Timed Out...\n", stderr);
		close(connect_fd);
        return -1;
	}
	
	//Secret Message Recieving 
	message_buffer = readline(connect_fd);
	
	//Disarm Timer
	timer_delete(timer_id);
	
	//Verify Correct Secret Message
	if(strcmp("<" SECRET ">\n", message_buffer) != 0){
		write(connect_fd, error_message, strlen(error_message));
		close(connect_fd);
		return -1;
	}

	//Final OK Message
	if(write(connect_fd, ok_message, strlen(ok_message)) < strlen(ok_message)){
		fputs("Imcomplete Write: Missing Data\n", stderr);
		close(connect_fd);
        return -1;
	}
	return 0;
}


void handle_bash(char *slave_name){

	//Create New Session ID
	if(setsid() == -1){
		fputs("Error Creating New Session ID\n", stderr);
		exit(EXIT_FAILURE);
	}

	//Open PTY Slave
	int slave_fd = open(slave_name, O_RDWR | O_CLOEXEC);
	if(slave_fd == -1){
		fputs("Error Opening Slave File Descriptor\n", stderr);
		exit(EXIT_FAILURE);
	}

	//Dup Redirection
	if(dup2(slave_fd, STDOUT_FILENO) == -1 || dup2(slave_fd, STDERR_FILENO) == -1 || dup2(slave_fd, STDIN_FILENO) == -1){	
		fputs("Error Dupping\n", stderr);
		close(slave_fd);
		exit(EXIT_FAILURE);
	}

	//Spawn Bash
	if(execlp("bash", "bash", NULL) == -1){ 
		fputs("Error Starting Bash from Server\n", stderr);
		close(slave_fd);
		exit(EXIT_FAILURE);
	}
}


int create_pty_master(char *slave_name){
	
	//Variable to Hold Slave and Master fd and name
	char *slave_temp;
	int master_fd;
	
	//Opent PTY Master File Descriptor
	if((master_fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1){
		fputs("Error Opening PTY\n", stderr);
		return -1;
	}
	
	//Set Up Close on Exec for Master File Descriptor
	if(fcntl(master_fd, F_SETFD, FD_CLOEXEC) == -1){
		fputs("Error Setting Up Close on Exec for Master\n", stderr);
		close(master_fd);
		return -1;
	}
	
	//Unlock Slave PTY File Descriptor
	if(unlockpt(master_fd) == -1){
		fputs("Error Unlocking PTY\n", stderr);
		close(master_fd);
		return -1;
	}
	
	//Get Slave Name 
	slave_temp = ptsname(master_fd);
	if(slave_temp == NULL){
		fputs("Error Getting Slave Name\n", stderr);
		close(master_fd);
		return -1;
	}
	
	//See if String is Able to Be Copied
	if(strlen(slave_temp) >= MAX_BUFF){
		fputs("Insufficient Storage for Slave Name\n", stderr);
		close(master_fd);
		return -1;
	}
	
	//Copy String to New Location
	strncpy(slave_name, slave_temp, MAX_BUFF);
	
	return master_fd;
}

void *epoll_select(){
	
	int ready, sourcefd;
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
				sourcefd = evlist[i].data.fd;
				if(transfer_data(sourcefd, fd_pairs[sourcefd]) == -1){
					fputs("Error Transfer Data between client and server fd\n", stderr);
				}
			}
		}
	}
	pthread_exit(NULL);
}

int transfer_data(int read_fd, int write_fd){
	
	static char *read_buffer; 
	int chars_read = 0;
	
	//Memory Allocation Error Checking
	if((read_buffer = malloc(sizeof(char) * MAX_BUFF)) == NULL){
		fputs("Error Allocating Memory\n", stderr);
		close(read_fd);
		close(write_fd);
		return -1;
	}
	
	//Read from File Descriptor
	if((chars_read = read(read_fd, read_buffer, MAX_BUFF)) <= 0){
		fputs("Error reading from File Descriptor\n", stderr);
		close(read_fd);
		close(write_fd);
		return -1;
	}
	
	//Write to File Descriptor
	if((write(write_fd, read_buffer, chars_read)) < chars_read){
		fputs("Did not complete Write System call\n", stderr);
		close(read_fd);
		close(write_fd);
		return -1;
	}
	return 0;
}


void timer_sig_handler(int sig){
	/*Once a timer is set off, it interrupts a blocking read call
	in the thread that called it*/
}
