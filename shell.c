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
void change_output(char* file_name);
void change_input(char* file_name);
int execute_command(char** command, char** files);
char** prepare_command_array(char** tokens, int array_length);
char** check_for_files(char** array, int array_length);

void implement_pipeline(char** first_command,char** second_command);

int array_length(char** array);

INPUT_PARSER* input_parser;

pid_t child_pid;

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

	int pipe_index = -1; 


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
		free(command);
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


	//go through our tokens array, check for redirection
	//so we know to change input/output in execute_command()
	for(int i = 0; i < start_index;i++)
	{
		if(strcmp("|",tokens[i]) == 0)
		{
			pipe_index = i;
		}
	}
	
	if(pipe_index != -1)
	{
		//first free our pipe_index
		//then set to null
		free(tokens[pipe_index]);
		tokens[pipe_index] = NULL;

		//implement pipeline
		implement_pipeline(tokens,&tokens[pipe_index+1]);

		//free our dynamically allocated variables before returning
		free_tokens(tokens);
		free_tokens(&tokens[pipe_index+1]);
		free_input_parser(input_parser);
		return;

	}

	char** potential_files = check_for_files(tokens,start_index);

	//this cleans our array, handling redirection if necessary
	char** final_command_array = prepare_command_array(tokens,start_index);

	
	//these next two lines may be a bit redundant
	//however, I think using exit() under an error was more
	//important so it was better to have a bit of 
	//duplicate code 
	if(execute_command(final_command_array,potential_files) == -1)
	{
		free_tokens(tokens);
		free(potential_files);
		free(final_command_array);
		free_input_parser(input_parser);
		exit(EXIT_FAILURE);
	}

	//free any dynamically allocated memory
	free(potential_files);
	free(final_command_array);
	free_tokens(tokens);
	free_input_parser(input_parser);
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

/**
 * Function represents a two stage pipeline
 * @param first command (first half before the pipe symbol)
 * @param second command (second half after the pipe symbol)
**/
void implement_pipeline(char** first_command, char** second_command)
{
	int fd[2];
	pid_t child_pid_1;
	pid_t child_pid_2;


	if(first_command == NULL || second_command == NULL)
	{
		perror("Pipe commands cannot be null");
		free(first_command);
		free(second_command);
		return;
	}


	//important: if pipe system call fails we should exit
	if(pipe(fd) == -1)
	{
		perror("Pipe error");
		free(first_command);
		free(second_command);
		free_input_parser(input_parser);
		exit(EXIT_FAILURE);
	}

	//code block represents our first child process running 
	//call fork and immediately exit if call fails
	child_pid_1 = fork();

	if(child_pid_1 == -1)
	{
		perror("Fork error");
		free(first_command);
		free(second_command);
		free_input_parser(input_parser);
		exit(EXIT_FAILURE);
	}

	if(child_pid_1 == 0)
	{
		close(fd[0]);

		//array maintenance:
		//want to check for files if necessary
		//also get the length to pass into the functions
		int first_command_len = array_length(first_command);

		char** files = check_for_files(first_command,first_command_len);

		char** cleaned_array = prepare_command_array(first_command,first_command_len);

		//change file descriptor and assign fils if necessary
		if(dup2(fd[1],STDOUT_FILENO) == -1)
		{
			perror("Cannot change pipe output");
			free(first_command);
			free(second_command);
			free(files);
			free(cleaned_array);
			free_input_parser(input_parser);
			exit(EXIT_FAILURE);
		}

		if(files[0] != NULL)
		{
			change_output(files[0]);
		}

		if(files[1] != NULL)
		{
			change_input(files[1]);
		}

		//finally execute the command and close file descriptor
		//free dynamically allocated variables
		if(execvp(cleaned_array[0],cleaned_array) == -1)
		{
			free(cleaned_array);
			free(first_command);
			free(second_command);
			free(files);
			free_input_parser(input_parser);
			perror("Cannot process command");
			exit(EXIT_FAILURE);
		}

		close(fd[1]);

	}


	//process is exactly the same as above except 
	// the different file descriptor
	child_pid_2 = fork();

	if(child_pid_2 == -1)
	{
		perror("Fork error");
		free(first_command);
		free(second_command);
		free_input_parser(input_parser);
		exit(EXIT_FAILURE);
	}

	if(child_pid_2 == 0)
	{
		close(fd[1]);

		int second_command_len = array_length(second_command);

		char** files = check_for_files(second_command,second_command_len);

		char** cleaned_array = prepare_command_array(second_command,second_command_len);


		if(dup2(fd[0],STDIN_FILENO) == -1)
		{
			perror("Cannot change pipe input");
			free(first_command);
			free(second_command);
			free(files);
			free(cleaned_array);
			free_input_parser(input_parser);
			exit(EXIT_FAILURE);
		}

		if(files[0] != NULL)
		{
			change_output(files[0]);
		}

		if(files[1] != NULL)
		{
			change_input(files[1]);
		}

		if(execvp(cleaned_array[0],cleaned_array) == -1)
		{
			perror("Cannot process command");
			free(first_command);
			free(second_command);
			free(files);
			free(cleaned_array);
			free_input_parser(input_parser);
			exit(EXIT_FAILURE);
		}

		close(fd[0]);
	}
	
	//close file descriptors and wait so no zombies
	close(fd[0]);
	close(fd[1]);
	waitpid(child_pid_1,NULL,0);
	waitpid(child_pid_2,NULL,0);

	child_pid_1 = 0;

	child_pid_2 = 0;


	return;


}

/**
 * Function to change the input of a command, used
 * in redirection and pipes
 * @param file name to be the new input
**/ 
void change_input(char* file_name)
{
	//open file as read only since this will be our input
	//handle errors if necessary
	int fd = open(file_name,O_RDONLY);

	if(fd < 0)
	{
		perror("Error opening input file");
		exit(EXIT_FAILURE);
	}

	if(dup2(fd,STDIN_FILENO) == -1)
	{
		perror("Error changing input to file from standard in");
		close(fd);
		exit(EXIT_FAILURE);
	}

	//remember to close if dup2() was successful
	close(fd);
}

/**
 * Function to change the output of a command, used
 * in redirection and pipes
 * @param file name where output will be sent
**/ 
void change_output(char* file_name)
{
	//open for writing if exists, if not create
	//also truncate
	int fd = open(file_name,O_WRONLY | O_CREAT | O_TRUNC,0644);

	if(fd < 0)
	{
		perror("Error opening destination file");
		exit(EXIT_FAILURE);
	}

	if(dup2(fd,STDOUT_FILENO) == -1)
	{
		perror("Error changing output destination");
		close(fd);
		exit(EXIT_FAILURE);
	}

	//remember close fd if dup2() was successful
	close(fd);
}

/**
 * Function cleans an array that has redirection
 * by skipping over the redirection symbol(s)
 * @param tokens, this is the command with redirection
 * @param array_length, this is the length of tokens
 * @return an array that will be sent to execute_command()
 * without redirection symbol(s)
**/ 
char** prepare_command_array(char* tokens[],int array_length)
{
	//handle null input
	if(tokens == NULL)
	{
		printf("%s\n","Cannot clean redirection array: input NULL");
		return NULL;
	}

	//code block will dynamically allocate the array, check for errors
	//then fill the array while skipping over redirection
	//and finally null terminate and return
	char** cleaned_array = malloc(sizeof(char*) * (array_length+1));

	if(cleaned_array == NULL)
	{
		perror("Error allocating memory for new array");
		exit(EXIT_FAILURE);
	}

	int arg_index = 0;

	for(int i = 0; i < array_length;i++)
	{
		if(strcmp(tokens[i],"<") == 0 || strcmp(tokens[i],">") == 0)
		{
			i++;
		}
		else
		{
			cleaned_array[arg_index++] = tokens[i];
		}
	}

	cleaned_array[arg_index] = NULL;

	return cleaned_array;
}


/**
 * Function will execute the command after calling fork() and execvp()
 * @param array, the command array that will be passed to execvp()
 * @param output_file_name name of output file (could be null if no
 * redirection)
 * @param input_file_name name of input file (could also be null if no 
 * redirection)
**/ 
int execute_command(char** array,char** files)
{

	//null check, immediately after call fork()
	if(array == NULL)
	{
		printf("%s\n","command array must not be null");
		return -1;
	}

	child_pid = fork();

	
	if(child_pid == -1)
	{
		perror("Fork failure");
		return -1;
	}

	//assuming no fork error, we should check if the file names are not null
	//so we can change input/output as necessary
	if(child_pid == 0)
	{
		if(files[0] != NULL)
		{
			change_output(files[0]);
		}

		if(files[1] != NULL)
		{
			change_input(files[1]);
		}

		//free our dynamically allocated variables
		//in case of execvp failure
		if(execvp(array[0],array) == -1)
		{
			perror("Could not execute command");
			return -1;
		}
	}

	//wait so there are no zombies
	waitpid(child_pid,NULL,0);
	child_pid = 0;

	return 0;

}

/**
 * Function will check if redirection files exist and return them if necessary
 * @param array, this is the command array, return NULL if null
 * @param array_length length of the input array
 * @return an array of size two with files if necessary 
 * or else both are NULL
**/ 
char** check_for_files(char** array,int array_length)
{

	//file placeholders
	char* input_file_name = NULL;
	char* output_file_name = NULL;

	if(array == NULL)
	{
		perror("Input array cannot be null");
		return NULL;
	}

	//go through the array, checking for redirection symbols so
	//we know to assign files
	for(int i = 0; i < array_length;i++)
	{
		if((strcmp(array[i],"<") == 0) && i+1 < array_length)
		{
			input_file_name = array[i+1];
		}

		if((strcmp(array[i],">") == 0) && i+1 < array_length)
		{
			output_file_name = array[i+1];
		}
	}

	//dynamically allocate and assign the files
	//possible that they are both null if no files
	char** files = malloc(sizeof(char*) * 2);

	files[0] = output_file_name;
	files[1] = input_file_name;

	return files;
}

/**
 * function to get the length of an array, handy for when
 * executing the pipe line since we are passing in two command arrays 
 * and not necessarily their length
 * @param command array
 * @return length of array if array is not NULL, otherwise -1
**/ 
int array_length(char** array)
{
	//NULL check
	if(array == NULL)
	{
		perror("Cannot find length of NULL array");
		return -1;
	}

	//iterate and return our array length
	int length = 0;

	while(array[length] != NULL)
	{
		length++;
	}

	return length;
}