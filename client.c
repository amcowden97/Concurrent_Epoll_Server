#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include "readline.c"

#define MAX_BUFF 4024
#define PORT 4070
#define SECRET "cs407rembash"

//Global Variables to Use with Signals and Lengths
struct termios saved_attributes;


int main(int argc, char *argv[]){
	
	//Function Prototypes
	int handle_rembash(int sockfd);
	void sigchild_handler(int sig);
	int setup_socket(char *address);
	int socket_input(int sockfd);
	int socket_output(int sockfd);
	int start_noncanon();
	int reset_terminal();
	
	//Command Line Argument Validation
	if(argc != 2){
		fputs("Incorrect Argument Number: Usage = IP Address as Argument\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Socket Intialization and Connection
	int sockfd;
	
	if((sockfd = setup_socket(argv[1])) == -1){
		fputs("Error Creating Socket\n", stderr);
		exit(EXIT_FAILURE);
	}
	 
	//Client / Server Initial Communication
	if(handle_rembash(sockfd) == -1){
		fputs("Error Validating Rembash Protocol\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Setting TTY into Noncanonical Mode
	if(start_noncanon() == -1){
		fputs("Error Setting Terminal into Noncanonical Mode\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Set up SIGCHILD Signal for Child Processes Terminating Before Parent 
	struct sigaction response;
	memset(&response, 0, sizeof(response));
	response.sa_handler = sigchild_handler;
	sigemptyset(&response.sa_mask);
	sigaction(SIGCHLD, &response, NULL);
	
	//Forking for Reading and Writing Loops
	int child_pid = fork();

	switch(child_pid){
		case 0:	//Read Commands from Terminal
			if(socket_input(sockfd) == -1){
				fputs("Error Reading Commands from the Terminal\n", stderr);
				exit(EXIT_FAILURE);
			}
			exit(EXIT_SUCCESS);
			
		case -1: //Error Forking Child
			fputs("Error Forking Child", stderr);
			exit(EXIT_FAILURE);
			
		default: //Output Bash Response to Terminal
			if(socket_output(sockfd) == -1){
				fputs("Error Reading from the Socket to STDOUT", stderr);
				exit(EXIT_FAILURE);
			}
			
			//Kill Child
			if(kill(child_pid, SIGKILL) == -1){
				fputs("Error Killing Child\n", stderr);
			}
			
			//Collect Child
			if(wait(NULL) == -1){
				fputs("Error Collecting Child\n", stderr);
				exit(EXIT_FAILURE);
			}
	}
	
	//Restore TTY Attributes
	if(reset_terminal() == -1){
		fputs("Error Establishing Original Terminal Settings\n", stderr);
		exit(EXIT_FAILURE);
	}
    exit(EXIT_SUCCESS);
}

	   
int setup_socket(char *wanted_address){
	
	//Creates and Catches Socket 
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	if(sockfd < 0){
		fputs("Unable to Create the Socket from System Call\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Address Initialization
	struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(wanted_address);
    address.sin_port = htons(PORT);

	//Connects Socket with Specified Address
    int connect_result = connect(sockfd, (struct sockaddr *) &address, sizeof(address));
	if(connect_result == -1){
        fputs("Unable to Connect the Address with the Specified Socket\n", stderr);
		exit(EXIT_FAILURE);
    }
	return sockfd;
}


int socket_input(int sockfd){
	char c;
    while(read(STDIN_FILENO, &c, sizeof(c))) {
		write(sockfd, &c, sizeof(c));
    }
	return EXIT_SUCCESS;
}


int socket_output(int sockfd){
	
	int chars_read;
	char *read_buffer;

	//Memory Allocation Error Checking
	if((read_buffer = malloc(sizeof(char) * MAX_BUFF)) == NULL){
		fputs("Error Allocating Memory\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	//Read from Socket to STDOUT
	while((chars_read = read(sockfd, read_buffer, MAX_BUFF)) > 0){
		if(write(STDOUT_FILENO, read_buffer, chars_read) < chars_read){
			fputs("Incomplete Write: Missing Data\n", stderr);
			exit(EXIT_FAILURE);
		}
	}
	return EXIT_SUCCESS;
}
	

int handle_rembash(int sockfd){
	
	const char * const rembash_message = "<rembash>\n";
	const char * const ok_message = "<ok>\n";
	
	//Rembash Protocol Verification
	char *message_buffer = readline(sockfd);
	
	if(strcmp(rembash_message, message_buffer) != 0){
		fputs("Incorrect Protocol Name\n", stderr);
		return EXIT_FAILURE;
	}
	
	//Secret Message Send
	int secret_length = strlen("<" SECRET ">\n");
    if(write(sockfd, "<" SECRET ">\n", secret_length) < secret_length){
		fputs("Incomplete Write: Missing Data\n", stderr);
		return EXIT_FAILURE;
	}
    
	//Checks Secert Message Server Response
	message_buffer = readline(sockfd);
	
    if(strcmp(ok_message, message_buffer) != 0){
		fputs("Incorrect Secret Message\n", stderr);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


int start_noncanon(){
	
	//Store Noncanonical Mode Attributes
	struct termios cbreak_attributes;
	
	//Store Default TTY Settings
	if(tcgetattr(STDIN_FILENO, &saved_attributes) == -1){
		fputs("Error Saving Default Terminal Characteristics\n", stderr);
		return EXIT_FAILURE;
	}
	
	tcgetattr (STDIN_FILENO, &cbreak_attributes);
	cbreak_attributes.c_lflag &= ~(ICANON | ECHO);
	cbreak_attributes.c_cc[VMIN] = 1;
	cbreak_attributes.c_cc[VTIME] = 0;
	
	//Connect Structure with Terminal
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &cbreak_attributes) == -1){
		fputs("Error Setting Terminal Attributes to Corresponding Terminal and File Descriptor\n", stderr);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


int reset_terminal(){
	//Connect Structure with Terminal
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_attributes) == -1){
		fputs("Error Setting Terminal Attributes to Corresponding Terminal and File Descriptor\n", stderr);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


void sigchild_handler(int sig){
	/*Once a child dies. This will ensure that the client parent terminates
	appropriatly setting back the terminal settings*/
	switch(sig){
		case SIGCHLD:
			reset_terminal();
			wait(NULL);
			exit(EXIT_SUCCESS);
	}
}