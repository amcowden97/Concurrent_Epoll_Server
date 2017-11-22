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
	void sigchild_handler(int sig);
	int handle_rembash(int sockfd);
	int setup_socket(char *address);
	int socket_input(int sockfd);
	int socket_output(int sockfd);
	int start_noncanon();
	int reset_terminal();
	
	//Command Line Argument Validation
	if(argc != 2){
		perror("\nIn Function (Main), Incorrect Number of Arguments. NOTE: This"
			   " Terminates The Client Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Socket Intialization and Connection
	int sockfd;
	
	if((sockfd = setup_socket(argv[1])) == -1){
		perror("\nIn Function (Main), Error Connecting Client End Of The Socket."
			   " Note: This Terminates The Client Program.\n");
		exit(EXIT_FAILURE);
	}
	 
	//Client / Server Initial Communication
	if(handle_rembash(sockfd) == -1){
		perror("\nIn Function (Main), Error Completing Rembash Protocol."
			   " Note: This Terminates The Client Program.\n");
		exit(EXIT_FAILURE);
	}
	
	//Setting TTY into Noncanonical Mode
	if(start_noncanon() == -1){
		perror("\nIn Function (Main), Error Setting Client In Noncanonical Mode."
			   " Note: This Terminates The Client Program.\n");
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
				perror("\nIn Function (Main), Could Not Read Characters From User."
					   " Note: This Terminates The Client Program.\n");
				exit(EXIT_FAILURE);
			}
			exit(EXIT_SUCCESS);
			
		case -1: //Error Forking Child
			perror("\nIn Function (Main), Error Forking Subprocess."
					   " Note: This Terminates The Client Program.\n");
			exit(EXIT_FAILURE);
			
		default: //Output Bash Response to Terminal
			if(socket_output(sockfd) == -1){
				perror("\nIn Function (Main), Could Not Output Character To Terminal."
					   " Note: This Terminates The Client Program.\n");
				exit(EXIT_FAILURE);
			}
			
			//Kill Child
			kill(child_pid, SIGKILL);
			
			//Collect Child
			if(wait(NULL) == -1){
				perror("\nIn Function (Main), Error Collecting Children."
					   " Note: This Terminates The Client Program.\n");
				exit(EXIT_FAILURE);
			}
	}
	
	//Restore TTY Attributes
	if(reset_terminal() == -1){
		perror("\nIn Function (Main), Could Not Restore Terminal Settings."
			   " Please Reset Or Close This Terminal For Proper Use."
			   " Note: This Terminates The Client Program.\n");
		exit(EXIT_FAILURE);
	}
    exit(EXIT_SUCCESS);
}

	   
int setup_socket(char *wanted_address){
	
	//Creates and Catches Socket 
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	if(sockfd < 0){
		perror("\nIn Function (setup_socket), Error Connecting Client Socket"
					   " To Wanted Address. Note: Error Exits Function.\n");
		return -1;
	}
	
	//Address Initialization
	struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(wanted_address);
    address.sin_port = htons(PORT);

	//Connects Socket with Specified Address
    int connect_result = connect(sockfd, (struct sockaddr *) &address, sizeof(address));
	if(connect_result == -1){
        perror("\nIn Function (setup_socket), Error Connecting Client Socket End"
					   " To The Server. Note: Error Exits Function.\n");
		return -1;
    }
	return sockfd;
}


int socket_input(int sockfd){
	char c;
    while(read(STDIN_FILENO, &c, sizeof(c))) {
		if(write(sockfd, &c, sizeof(c)) < 0){
			perror("\nIn Function (socket_input), Error Writing Characters Read"
					   " From User. Note: Error Exits Function.\n");
		return -1;
		}
    }
	return 0;
}


int socket_output(int sockfd){
	
	int chars_read;
	char *read_buffer;

	//Memory Allocation Error Checking
	if((read_buffer = malloc(sizeof(char) * MAX_BUFF)) == NULL){
		perror("\nIn Function (socket_output), Error Allocating Memory To Hold"
			   " Characters Read. Note: Error Exits Function.\n");
		return -1;
	}
	
	//Read from Socket to STDOUT
	while((chars_read = read(sockfd, read_buffer, MAX_BUFF)) > 0){
		if(write(STDOUT_FILENO, read_buffer, chars_read) < chars_read){
			perror("\nIn Function (socket_output), Error Writing Characters Read"
					" From PTY Master. Note: Error Exits Function.\n");
			return -1;
		}
	}
	return 0;
}
	

int handle_rembash(int sockfd){
	
	const char * const rembash_message = "<rembash>\n";
	const char * const ok_message = "<ok>\n";
	
	//Rembash Protocol Verification
	char *message_buffer = readline(sockfd);
	
	if(strcmp(rembash_message, message_buffer) != 0){
		perror("\nIn Function (handle_rembash), Incorrect Rembash Message."
			   " Note: Error Exits Function.\n");
		return -1;
	}
	
	//Secret Message Send
	int secret_length = strlen("<" SECRET ">\n");
    if(write(sockfd, "<" SECRET ">\n", secret_length) < secret_length){
		perror("\nIn Function (handle_rembash), Incorrect Secret Message."
			   " Note: Error Exits Function.\n");
		return -1;
	}
    
	//Checks Secert Message Server Response
	if((message_buffer = readline(sockfd)) == NULL){
		perror("\nIn Function (handle_rembash), Error Reading From Socket"
			   " Note: Error Exits Function.\n");
		return -1;
	}
	
    if(strcmp(ok_message, message_buffer) != 0){
		perror("\nIn Function (handle_rembash), Could Not Send OK Message"
			   " Note: Error Exits Function.\n");
		return -1;
	}
	return 0;
}


int start_noncanon(){
	
	//Store Noncanonical Mode Attributes
	struct termios cbreak_attributes;
	
	//Store Default TTY Settings
	if(tcgetattr(STDIN_FILENO, &saved_attributes) == -1){
		perror("\nIn Function (start_noncanon), Could Not Save Terminal"
			   " Attributes. Note: Error Exits Function.\n");
		return -1;
	}
	
	tcgetattr (STDIN_FILENO, &cbreak_attributes);
	cbreak_attributes.c_lflag &= ~(ICANON | ECHO);
	cbreak_attributes.c_cc[VMIN] = 1;
	cbreak_attributes.c_cc[VTIME] = 0;
	
	//Connect Structure with Terminal
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &cbreak_attributes) == -1){
		perror("\nIn Function (start_noncanon), Could Not Set Terminal"
			   " Attributes. Note: Error Exits Function.\n");
		return -1;
	}
	return 0;
}


int reset_terminal(){
	//Connect Structure with Terminal
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_attributes) == -1){
		perror("\nIn Function (start_noncanon), Could Not Restore Terminal"
			   " Attributes. Note: Error Exits Function.\n");
		return -1;
	}
	return 0;
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