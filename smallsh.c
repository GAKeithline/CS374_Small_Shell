// Name: Gilbert Keithline
// OSU Email: keithlig@oregonstate.edu
// Assignment: CS374 Programming Assignment 4 - SMALLSH
// Date: 2/27/2025
/* Description: Program implements a limited shell that emulates standardized shells,
		such as bash. Smallsh features 3 built-in commands, 'exit', 'cd', and 'status',
		that are all handled locally within the program. All other commands of the
		format "command [arg1 arg2 ...] [< input_file] [> output_file] [&]" are
		sent to a parser function (the brackets are ommitted).
		The input "command [arg1 arg2 ...]" are sent to an execvp() function
		inside of a child process. The input "[< input_file] [> output_file]" is
		used to redirect the input and output respecively. The input "[&]"
		indicates the command is to be run as a background process.
		Smallsh includes custom handlers for SIGINT and SIGTSTP. Background
		processes, as well as the shell itself, ignore SIGINT (ctrl-c) while foreground
		processes default to SIGINT. Foreground and background processes
		both ignore SIGTSTP (ctrl-z), while the shell interprets SIGTSTP as a call
		to toggle in and out of "foreground-only mode" wherein background-processes
		are run in the foreground until the mode is toggled off again.
*/

// Citations
//------------------------------------------------------------------------------------------------------
/*  
	Citation for struct command_line and parse_input() function
    Adapted from: Parser code provided by OSU CS374 instructor staff in "Programming Assignment 4: SMALLSH"
    URL: https://canvas.oregonstate.edu/courses/1987883/assignments/9864854?module_item_id=24956222
    Date Retrieved: 2/18/2025

	Citation for SIGSTP and SIGINT sigactions and handle_SIGTSTP()
    Adapted from: Sample code provided by OSU CS374 instructor staff in "Exploration: Signal Handling API"
    URL: https://canvas.oregonstate.edu/courses/1987883/pages/exploration-signal-handling-api?module_item_id=24956227
    Date Retrieved: 2/23/2025

	Citation for change_dir() function - use of chdir() and getcwd()
    Adapted from: GeeksforGeeks- chdir() in C language with Examples
    URL: https://www.geeksforgeeks.org/chdir-in-c-language-with-examples/
    Date Retrieved: 2/20/2025

	Citation for execute() - forking and use of execvp()
    Adapted from: Sample code provided by OSU CS374 instructor staff in "Exploration: Process API - Executing a New Program"
    URL: https://canvas.oregonstate.edu/courses/1987883/pages/exploration-process-api-executing-a-new-program?module_item_id=24956220
    Date Retrieved: 2/20/2025
*/
//------------------------------------------------------------------------------------------------------

// Headers
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

// Macros
#define INPUT_LENGTH 2048
#define MAX_ARGS		 512

// Global Structs
typedef struct command_line
{
	char *argv[MAX_ARGS + 1];			// An array for parsing the user-input into
	char *passArgs[MAX_ARGS + 1];			// An array that holds only the commands to be passed to execvp
	int numArgs;					// Tracks the number of args to be passed to execvp
	int argc;					// Tracks the total number of space-delimited arguments entered in the input
	bool chInput;					// Flag that indicates if a change of input is necessary
	char *input_file;				// Holder for input path
	bool chOutput;					// Flag that indicates if a change of output is necessary
	char *output_file;				// Holder for output path
	bool is_bg;					// Flag that indicates if command is to be run in the background (ignored in Foreground-Only mode)
}command_line;

// Global Variables
int childStatus;				// Holder variable for the most recently ended Foreground Process status code
int fg_pid = 0;					// Holder variable fort the most recently ended Foreground Process pid
int bgps[100] = {0};				// Maximum number of background processes allowed/trackable by the shell
bool allow_bg = true;				// Flag that indicates whether the shell allows background processes or if it is in foreground-only mode

// Signal Handlers
void handle_SIGTSTP(int signo){
	/*
	Provides an alternate response when SIGTSTP (ctrl-z) is received.
	Checks 'allow_bg' flag and sets it to the opposite value.
	*/
	if(allow_bg){
		char *bkgOff = "\nEntering foreground-only mode (& is now ignored)\n: ";
		write(STDOUT_FILENO, bkgOff, strlen(bkgOff));		// use 'write' for reentry
		allow_bg = false;
	}else{
		char *bkgOn = "\nExiting foreground-only mode\n: ";
		write(STDOUT_FILENO, bkgOn, strlen(bkgOn));
		allow_bg = true;
	}
}

// Prototypes
void command_switch(struct command_line* curr_command);
void fg_status();
void check_bgps();

// Funcs
//command_line 
void parse_input(char input[INPUT_LENGTH]){
	/*
	Accepts a char array and parses the contents to correctly populate the
	the argv[], input, output, and other background metadata into their
	respective data members within the command_line struct.
	** Adapted from Parser Code provided by CS374 intructor staff.
	*/

	// Local Variables and Structs - parse_input
	char *check_bg;													// A holder for the final character bearing token
	struct command_line *curr_command = (struct command_line *) calloc(1, sizeof(struct command_line));		// The command line that all of the input data will be organized into

	// Initialize flags and empty values in command_line
	curr_command->is_bg = false;
	curr_command->chInput = false;
	curr_command->chOutput = false;
	curr_command->numArgs = 0;

	// Tokenize the input and check for empty/comment status
	char *token = strtok(input, " \n");
	while(token){
		check_bg = strdup(token);
		if(!strcmp(token,"<")){  // Check for input
			curr_command->input_file = strdup(strtok(NULL," \n"));
			curr_command->chInput = true;
		} else if(!strcmp(token,">")){  // Check for output
			curr_command->output_file = strdup(strtok(NULL," \n"));
			curr_command->chOutput = true;
		}else{
			curr_command->argv[curr_command->argc++] = strdup(token);
		}
		if(!curr_command->chInput && !curr_command->chOutput){  // only store arguments to be passed to an external function
			curr_command->passArgs[curr_command->numArgs] = token;
			++curr_command->numArgs;
		}
		token=strtok(NULL," \n");
	}
	// Check the final non-NULL token for '&'
	if(strcmp(check_bg,"&")==0){ 
		curr_command->is_bg = true;
	}
	// Remove '&', if present, from passArgs and set a NULL value for execvp
	if(!curr_command->chInput && !curr_command->chOutput){
		if(curr_command->is_bg){
			--curr_command->numArgs;
			curr_command->passArgs[curr_command->numArgs] = NULL;
		}
	}
	// If in foreground only mode, revoke background flag
	if(!allow_bg){
		curr_command->is_bg = false;
	}
	free(token);
	free(check_bg);
	command_switch(curr_command);
	}


void end_prog(){
	/*
	Checks array of background PIDs and exits any found
	running before terminating the shell with exit code 0.
	*/
	
	// Local Variables - end_prog()
	int checkSize = 100 * sizeof(int);				// Represents max size of bgps array for use with 'for' loop

	// Send a Kill signal to all remaining background PIDs
	for(int i = 0; i < checkSize; i++){
		if(bgps[i] != 0){
			kill(bgps[i], SIGKILL);  // SIGKILL is is not catchable. SIGKILL is absolute, inevitable.
		}
	}
	// Send exit value '0' to trigger and end to the shell loop in main()
	exit(0);
}

void change_dir(int argc, char** argv){
	/*
	Accepts an argument count and array of arguments. If <2 arguments
	(a command), the working directory is changed to the HOME
	environment variable using chdir(). If 2 arguments (a command and path)
	the working directory is changed to the path specified in the second
	argument. Prints the new working directory on success. Prints an
	error message on failure.
	*/

	// Local Variables - change_dir()
	int changeStatus;						// Holder for the status code returned by chdir() call

	// Check arguments and call chdir()
	if(argc<2){
		changeStatus = chdir(getenv("HOME"));
	}else if(argc==2){
		changeStatus = chdir(argv[1]);
	}
	// Check changeStatus and return appropriate message
	if(changeStatus==0){
		char workingDirectory[300];
		printf("%s\n", getcwd(workingDirectory, sizeof(workingDirectory)));
	}else{
		printf("cd failed\n");
	}
	fflush(stdout); // Always flush outputs 
}

void add_bgp(int pid){
	/*
	Accepts an integer, representing a pid, and inserts it into
	the global variable 'bpgs' array.
	*/

	// Local Variables - add_bgp()
	int checkSize = 100 * sizeof(int);				// Represents max size of bgps array for use with 'for' loop

	// Add the pid to the next non-zero space in the array
	for(int i = 0; i < checkSize; i++){
		if(bgps[i] == 0){
			bgps[i] = pid;
			break; // parking spot found, stop searching
		}
	}
}

void check_bgps(){
	/*
	Combs the bgps array and checks for finished background procedures.
	If finished, a messgage containing the pid and exit code/term signal
	is printed to the terminal and the closed pid is removed from the array.
	*/

	// Local Variables - check_bgps()
	int checkSize = 100 * sizeof(int);				// Represents max size of bgps array for use with 'for' loop
	int statusCode;							// Status code holder variable for use with waitpid()

	// Comb bgps for non-zero pid value
	for(int i = 0; i < checkSize; i++){
		if(bgps[i] == 0){
			continue;
		}else{ // Check pid status with waitpid() and print appropriate message
			pid_t check = waitpid(bgps[i], &statusCode, WNOHANG);
			if(check > 0){
				printf("background pid %d is done: ", bgps[i]);
				fflush(stdout);
				if(WIFEXITED(statusCode)){ // print the exit message
					printf("exit value %d\n", WEXITSTATUS(statusCode));
				}else{ // print the terminated message
					printf("terminated by signal %d\n", WTERMSIG(statusCode));
				}
				fflush(stdout);
				bgps[i] = 0; // Remove closed PID
			}
		}
	}
}

void sigint_status(){
	/*
	A status checker specifically to track when a foreground process
	is killed with SIGINT. If the foreground process PID is found
	to have been killed with SIGINT, a termination message is printed.
	*/
	if(fg_pid != 0){ // references global holder variable
		if(WTERMSIG(childStatus) == 2){
			printf("terminated by signal %d\n", WTERMSIG(childStatus));
			fflush(stdout);
		}
		fg_pid = 0; // resets holder
	}
}

void fg_status(){
	/*
	Collects the value of the most recently closed foreground
	process and prints an exit/termination message.
	*/
	if(WIFEXITED(childStatus)){ // references global holder variable
		printf("exit value %d\n", WEXITSTATUS(childStatus));
	}else{
		printf("terminated by signal %d\n", WTERMSIG(childStatus));
	}
	fflush(stdout);
}

void execute(struct command_line* curr_command){
	/*
	Accepts a command_line object carries out the instructions by forking
	a child process and calling execvp. Declares custom signal handlers for 
	SIGINT and SIGTSTP to pass through to the child process.
	*/
	
	// Local Variables - execute()
	pid_t spawnid;							// Holder for the child pid

	// Fork off child for external functions
	spawnid = fork();
	switch(spawnid) {
		// In case of failure
		case -1:
			perror("fork() failed!\n");
			exit(1);
			break;
		// Child
		case 0:
		// Set custom handlers for bg/fg response to SIGINT and SIGTSTP
			if(!curr_command->is_bg){
				struct sigaction sigint_child = {0};
				sigint_child.sa_handler = SIG_DFL; // SIGINT honored by FG process
				sigfillset(&sigint_child.sa_mask);
				sigint_child.sa_flags = 0;
				sigaction(SIGINT, &sigint_child, NULL);

				struct sigaction sigstp_child = {0};
				sigstp_child.sa_handler = SIG_IGN; // SIGTSTP ignored by all processes
				sigfillset(&sigstp_child.sa_mask);
				sigstp_child.sa_flags = 0;
				sigaction(SIGTSTP, &sigstp_child, NULL);
			}else{
				struct sigaction sigstp_child = {0};
				sigstp_child.sa_handler = SIG_IGN; // SIGTSTP ignored by all processes
				sigfillset(&sigstp_child.sa_mask);
				sigstp_child.sa_flags = 0;
				sigaction(SIGTSTP, &sigstp_child, NULL);
			}

			// Redirect input if specified by chInput flag
			if(curr_command->chInput){
				int input_desc;
				input_desc = open(curr_command->input_file, O_RDONLY); // input is read only
				if(input_desc < 0){ 
					printf("cannot open %s for input\n", curr_command->input_file);
					fflush(stdout);
					exit(1);
				}
				dup2(input_desc, 0);
			}
			// Redirect output if specified by chOuput flag
			if(curr_command->chOutput){
				int output_desc;
				output_desc = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644); // output is write only with create/truncate
				if(output_desc < 0){
					printf("cannot open %s for output\n", curr_command->output_file);
					fflush(stdout);
					exit(1);
				}
				dup2(output_desc, 1);
			}
			// Execute command using execvp to check the PATH
			execvp(curr_command->passArgs[0], curr_command->passArgs);
			perror(curr_command->passArgs[0]); // only returns on error
			fflush(stdout);
			exit(1);
			break;
		// Parent
		default:
			// Print background pid and add it to bgps array
			if(curr_command->is_bg){
				printf("background PID is %d\n", spawnid);
				fflush(stdout);
				add_bgp(spawnid);
			}
		else{
			// Foreground wait, set fg_pid holder for SIGINT check
			waitpid(spawnid, &childStatus, 0);
			fg_pid = spawnid;
		}
	}
	// Check to see if the foreground process was ended with ctrl-c
	sigint_status();
}

void command_switch(struct command_line* curr_command){
	/*
	Accepts a command_line object and directs it to the appropriate helper
	function based on its contents.
	*/
	// Built in commands
	if(!strcmp(curr_command->argv[0],"exit")){
		end_prog();
	} else if(!strcmp(curr_command->argv[0],"status")){
		fg_status();
	} else if(!strcmp(curr_command->argv[0],"cd")){
		change_dir(curr_command->argc, curr_command->argv);
	} else {
		// External commands
		execute(curr_command);
	}
}

int main(){
	/*
	Main function that implements the Small Shell. Declares custom
	signal handlers for SIGTSTP and SIGINT and uses a while-loop to
	perpetually grab input from the user, only exiting when an exit(0)
	is called from within the loop.
	*/

	// Custom handlers
	struct sigaction sigstp_action = {0};
	sigstp_action.sa_handler = handle_SIGTSTP; // The shell calls a special response for ctrl-z
	sigfillset(&sigstp_action.sa_mask);
	sigstp_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &sigstp_action, NULL);

	struct sigaction sigint_action = {0};
	sigint_action.sa_handler = SIG_IGN; // The shell itself ignores ctrl-c
	sigfillset(&sigint_action.sa_mask);
	sigint_action.sa_flags = 0;
	sigaction(SIGINT, &sigint_action, NULL);

	// Shell loop
	while(true)
	{
		// Loop Variables - Local to main()
		char *parseString;					// Holder for use with strtok so the raw input is not destroyed
		char isComment;						// Holder for the first character of the input token for strcmp with '#'
		bool inputIgnore = false;				// Flag that determines if the input will be sent to the command parser
		char input[INPUT_LENGTH];				// Empty array to hold user input

		// Gather user input
		printf(": ");
		fflush(stdout);
		fgets(input, INPUT_LENGTH, stdin);
		fflush(stdin);
		// Check for blank cmd or "#' character. Set ignore flag if either are true.
		parseString = strdup(input);
		char *token = strtok(parseString, " \n");
		if(token == NULL){
			inputIgnore = true;
		}else{
			isComment = *token;  // Isolate the first character of the token for comparison
			if(!strcmp(&isComment,"#")){
				inputIgnore = true;
			}
		}
		// Run commands
		if(!inputIgnore){
			parse_input(input);
		}
		// Check background status
		check_bgps();

	}
	return EXIT_SUCCESS;
}
