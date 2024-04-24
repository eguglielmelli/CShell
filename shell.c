#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "input_parser.h"
#include <fcntl.h>
#include <string.h>

#define MAX_LINE 4096

void run_shell();
void register_signal_handler();
void kill_child_process();
void sig_int_handler(int handler);

pid_t child_pid;

INPUT_PARSER* input_parser;

/**
 * Function gets the command from the user and 
 * calls the remove whitespace to return a cleaned command
 * @return a trimmed command, NULL if errors on malloc
 * or read
**/
char* get_command()
{
	char* buf = malloc(MAX_LINE);

	//exit if malloc() error since we need commmand from user to proceed
	if(buf == NULL)
	{
		perror("Could not allocate memory for buffer");
		exit(EXIT_FAILURE);
	}

	int bytes_read = read(STDIN_FILENO,buf,MAX_LINE-1);

	//read error handling
	if(bytes_read < 0)
	{
		perror("Error reading from standard in");
		return NULL;
	}

	//remember to free buffer here if user
	//enters control + d
	if(bytes_read == 0)
	{
		free(buf);
		exit(EXIT_SUCCESS);
	}

	//null terminate, remove front/end whitespace, and free allocated buffer
	buf[bytes_read] = '\0';

	char* cleaned_command = remove_whitespace(buf);

	free(buf);

	return cleaned_command;
}

int main()
{
	register_signal_handler();
	while(1)
	{
		run_shell();
	}
	return(0);

}

/**
 * Method will run our shell which is called in a loop in main
 * Will exit program if errors on system calls such as fork() or execvp()
**/
void run_shell()
{
	//will hold our tokens, providing token space to user
	char* tokens[1000];


	//continuous arrow printing to the console using write()
	char* arrow = "enter command here: > ";

	if(write(STDOUT_FILENO,arrow,strlen(arrow)) == -1)
	{
		perror("Error writing to std out");
		exit(EXIT_FAILURE);
	}

	char* command = get_command();


	if(command == NULL) return;

	//hard code for exit out of shell, also ctrl + d works
	if(strcmp(command,"exit") == 0)
	{
		exit(EXIT_SUCCESS);
	}


	input_parser = init_input_parser(command);

	free(command);

	int start_index = 0;

	char* token = get_token(input_parser);

	//grab our tokens, store them in array initialized above
	while(token != NULL)
	{
		tokens[start_index] = token;
		start_index++;
		token = get_token(input_parser);
	}

	//null terminate tokens, very important for execvp() call
	tokens[start_index] = NULL;

	//now that we have commands, fork the child process
	child_pid = fork();
	if(child_pid == -1)
	{
		perror("Error in forking child process");
		exit(EXIT_FAILURE);
	}

	if(child_pid == 0)
	{

		if(execvp(tokens[0],tokens) == -1)
		{
			perror("Exec Error: Could not execute command");
			exit(EXIT_FAILURE);
		}
	}

	//wait for child process to finish, avoid zombies
	waitpid(child_pid,NULL,0);
	child_pid = 0;

	//free any allocated variables here
	free_input_parser(input_parser);
	free_tokens(tokens,start_index);
	return;
}

/**
 * Registers the signal handler, uses sig_int_handler method which calls
 * kill_child_process() if child process is still running
**/
void register_signal_handler()
{
	if(signal(SIGINT,sig_int_handler) == SIG_ERR)
	{
		perror("Could not register signals");
		exit(EXIT_FAILURE);
	}
}

/**
 * Function will kill the child process if it is
 * running when receiving SIGINT signal
**/ 
void sig_int_handler(int handler)
{
	if(child_pid != 0)
	{
		kill_child_process();
	}
}

/**
 * Kills the child process by sending 
 * kill() signal, exits program if error
**/ 
void kill_child_process()
{
	if(kill(child_pid,SIGKILL) == -1)
	{
		perror("Error killing child process");
		exit(EXIT_FAILURE);
	}
}


void implement_pipeline(char** first_command, char** second_command)
{

}