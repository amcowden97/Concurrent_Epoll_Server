#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE >= 199309L
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <time.h>
#include "readline.c"

#define MAX_BUFF 4024
#define MAX_TIME_IMPL 30
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
void *epoll_select();
int verify_rembash(int connect_fd);
void handle_bash(char *slave_name);
int open_pty_master(char *slave_name);
int transfer_data(int read_fd, int write_fd);


int main(){
		
	//Variable Declarations for Server Socket
    int server_sockfd, client_sockfd;
    socklen_t client_len;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
	
	//Variable Declarations for Threads
	pthread_t rw_thread, rembash_thread;
	
	//Socket Initialization
    if((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		fputs("Error Creating Socket\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Set Up Close on Exec for Server Socket File Descriptor
	if(fcntl(server_sockfd, F_SETFD, FD_CLOEXEC) == -1){
		fputs("Error Setting Up Close on Exec for Server Socket File Descriptor\n", stderr);
		exit(EXIT_FAILURE);
	}

	//Address Initialization
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT);
	
	//Bind Address with Socket
    if(bind(server_sockfd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
		fputs("Unable to Bind Address to Socket\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Listen for Connections on Socket
    if(listen(server_sockfd, 5) == -1){
		fputs("Unable to Mark the Socket as Listening\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Set Port Reuse in Abnormal Termination
	int i=1;
	setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
   
	//Eliminate Need for Child Proccess Collection
	if(signal(SIGCHLD, SIG_IGN) == SIG_ERR){
		fputs("Unable to Set Up Signal\n", stderr);
		exit(EXIT_FAILURE);
	}
		
	//Make Epoll Unit to Transfer client socket to PTY Master
	if ((epoll_fd = epoll_create(1)) == -1) {
		fputs("Error Creating Epoll Unit\n", stderr);
		exit(EXIT_FAILURE); 
	}
	
	//Set Up Close on Exec for Epoll Unit File Descriptor
	if(fcntl(epoll_fd, F_SETFD, FD_CLOEXEC) == -1){
		fputs("Error Setting Up Close on Exec for Epoll File Descriptor\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Create Thread to Handle File Descriptor Selection and Read/Write
	if(pthread_create(&rw_thread, NULL, epoll_select, NULL) == -1){
		fputs("Error Creating Thread for Epoll IO", stderr);
		exit(EXIT_FAILURE); 
	}

	//Infinite Server Loop to Handle Clients
    while(1){
        
		//Accept Client
        client_len = sizeof(client_address);
        if((client_sockfd = accept(server_sockfd, (struct sockaddr *) &client_address, &client_len)) == -1){
			fputs("Unable to Accept Client Socket\n", stderr);
			close(client_sockfd);
			continue;
		}
		
		//Set Up Close on Exec for Client File Descriptor
		if(fcntl(client_sockfd, F_SETFD, FD_CLOEXEC) == -1){
			fputs("Error Setting Up Close on Exec for Client FD\n", stderr);
			close(client_sockfd);
			continue;
		}
		
		//Save Client File Descriptor for Thread Process
		int *fdptr;
		if((fdptr = malloc(sizeof(int))) == NULL){
			fputs("Error Allocating Memory for Storing Client FD\n", stderr);
			close(client_sockfd);
			continue;
		}
		
		*fdptr = client_sockfd;
		
		//Create Thread for Verifying Rembash Protocol
		if(pthread_create(&rembash_thread, NULL, handle_client, fdptr) == -1){
        	fputs("Error Creating Thread for Verifying Rembash Protocol", stderr);
			close(client_sockfd);
			continue;
		}
	}
	exit(EXIT_SUCCESS);
}


void *handle_client(void *arg){
	
	int master_fd, connect_fd;
	char *slave_name;
	
	//Get Client File Descriptor
	connect_fd = *(int*)arg;
	free(arg);
	
	//Verify Valid Client
	if(verify_rembash(connect_fd) == EXIT_FAILURE){
		fputs("Critical Rembash Error\n", stderr);
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
	if((master_fd = open_pty_master(slave_name)) == -1){
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


int verify_rembash(int connect_fd){
	
	//Variables for Reading and Writing
	char *message_buffer;
	const char * const rembash_message = "<rembash>\n";
	const char * const error_message = "<error>\n";
	const char * const ok_message = "<ok>\n";
	
	//Timer Variables
	timer_t timer_id;
	struct sigevent signal_ev;
	struct itimerspec time_specs;

	//Rembash Send
	if(write(connect_fd, rembash_message, strlen(rembash_message)) < strlen(rembash_message)){
		fputs("Imcomplete Write: Missing Data\n", stderr);
		close(connect_fd);
        return EXIT_FAILURE;
	}

	//Set Up Timer Signal Handler
	struct sigaction response;
	memset(&response, 0, sizeof(response));
	response.sa_handler = timer_sig_handler;
	sigemptyset(&response.sa_mask);
	sigaction(TIMER_SIG, &response, NULL);

	//Create POSIX Timer
	sev.sigev_notify = SIGEV_THREAD_ID;
	sev.sigev_signo = TIMER_SIG;
	sev.sigev_value.sival_ptr = &timerid;
	
	//Get Correct Thread ID
	sev._sigev_un._tid = syscall(SYS_gettid);	//Gets thread ID of POSIX Thread
	
	//Create Timer for DOS Attacks
	if(timer_create(CLOCKID, &sev, &timerid) == -1){
		fputs("Error Creating Timer\n", stderr);
		return EXIT_FAILURE;
	}
	
	//Set Up Timer Length
	time_specs->it_value.tv_sec = MAX_TIME_IMPL;
	time_specs->it_value.tv_nsec = 0;
	
	//Set Timer Length
	if(timer_settime(timer_id, 0 &time_specs, NULL) == -1){
		fputs("Error Setting the Time for the POSIX Timer\n", stderr);
		return EXIT_FAILURE;
	}
	
	//Secret Message Recieving 
	message_buffer = readline(connect_fd);
	
	//Disarm Timer
	timer_delete(timer_id);
	
	//Verify Correct Secret Message
	if(strcmp("<" SECRET ">\n", message_buffer) != 0){
		write(connect_fd, error_message, strlen(error_message));
		close(connect_fd);
		return EXIT_FAILURE;
	}

	//Final OK Message
	if(write(connect_fd, ok_message, strlen(ok_message)) < strlen(ok_message)){
		fputs("Imcomplete Write: Missing Data\n", stderr);
		close(connect_fd);
        return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


void handle_bash(char *slave_name){

	//Create New Session ID
	if(setsid() == -1){
		fputs("Error Creating New Session ID\n", stderr);
		exit(EXIT_FAILURE);
	}

	//Open PTY Slave
	int slave_fd = open(slave_name, O_RDWR);
	if(slave_fd == -1){
		fputs("Error Opening Slave File Descriptor\n", stderr);
		exit(EXIT_FAILURE);
	}

	//Set Up Close on Exec for Slave File Descriptor
	if(fcntl(slave_fd, F_SETFD, FD_CLOEXEC) == -1){
		fputs("Error Setting Up Close on Exec for Slave\n", stderr);
		close(slave_fd);
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


int open_pty_master(char *slave_name){
	
	//Variable to Hold Slave and Master fd and name
	char *slave_temp;
	int master_fd;
	
	//Opent PTY Master File Descriptor
	if((master_fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1){
		fputs("Error Opening PTY\n", stderr);
		return EXIT_FAILURE;
	}
	
	//Set Up Close on Exec for Master File Descriptor
	if(fcntl(master_fd, F_SETFD, FD_CLOEXEC) == -1){
		fputs("Error Setting Up Close on Exec for Master\n", stderr);
		close(master_fd);
		return EXIT_FAILURE;
	}
	
	//Unlock Slave PTY File Descriptor
	if(unlockpt(master_fd) == -1){
		fputs("Error Unlocking PTY\n", stderr);
		close(master_fd);
		return EXIT_FAILURE;
	}
	
	//Get Slave Name 
	slave_temp = ptsname(master_fd);
	if(slave_temp == NULL){
		fputs("Error Getting Slave Name\n", stderr);
		close(master_fd);
		return EXIT_FAILURE;
	}
	
	//See if String is Able to Be Copied
	if(strlen(slave_temp) >= MAX_BUFF){
		fputs("Insufficient Storage for Slave Name\n", stderr);
		close(master_fd);
		return EXIT_FAILURE;
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
		for (int index = 0; index < ready; index++) {
	
			//Check if Epoll was Invalid
			if (evlist[index].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
				close(evlist[index].data.fd);
				close(fd_pairs[evlist[index].data.fd]);
				
			}else if (evlist[index].events & EPOLLIN) {
				//Data is ready to read so transfer:
				sourcefd = evlist[index].data.fd;
				if(transfer_data(sourcefd, fd_pairs[sourcefd]) == -1){
					fputs("Error Transfer Data between client and server fd\n", stderr);
				}
			}
		}
	}
	pthread_exit(NULL);
}

int transfer_data(int read_fd, int write_fd){
	
	int chars_read = 0;
	char *read_buffer; 
	
	//Memory Allocation Error Checking
	if((read_buffer = malloc(sizeof(char) * MAX_BUFF)) == NULL){
		fputs("Error Allocating Memory\n", stderr);
		close(read_fd);
		close(write_fd);
		return EXIT_FAILURE;
	}
	
	//Read from File Descriptor
	if((chars_read = read(read_fd, read_buffer, MAX_BUFF)) <= 0){
		fputs("Error reading from File Descriptor\n", stderr);
		close(read_fd);
		close(write_fd);
		return EXIT_FAILURE;
	}
	
	//Write to File Descriptor
	if((write(write_fd, read_buffer, chars_read)) < chars_read){
		fputs("Did not complete Write System call\n", stderr);
		close(read_fd);
		close(write_fd);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


void timer_sig_handler(int sig){
	/*Once a timer is set off, it interrupts a blocking read call
	in the thread that called it*/
}
