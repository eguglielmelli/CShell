#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include "input_parser.h"
#include "shell.h"
#include "utils.h"

#define MAX_LINE 4096

INPUT_PARSER* input_parser;

pid_t child_pid;

bg_proc_manager_t bg_proc_manager;
process_t* bg_processes[MAX_BG_PROC];
command_history_t command_history;

/**
 * Function gets the command from the user and 
 * calls the remove whitespace to return a cleaned command
 * @return a trimmed command, NULL if errors on malloc
 * or read
**/
char* get_command()
{
	char* buf = malloc(MAX_LINE);

	if(!buf)
	{
		perror("Could not allocate memory for buffer");
		exit(EXIT_FAILURE);
	}

	int bytes_read = read(STDIN_FILENO,buf,MAX_LINE-1);

	if(bytes_read < 0)
	{
		perror("Error reading from standard in");
		return NULL;
	}

	if(bytes_read == 0)
	{
		free(buf);
		free_history(&command_history);
		exit(EXIT_SUCCESS);
	}

	buf[bytes_read] = '\0';

	char* cleaned_command = remove_whitespace(buf);

	free(buf);

	return cleaned_command;
}

int main()
{
	register_signal_handler();
	register_sig_chld_handler();
	init_bg_proc_manager(&bg_proc_manager);
	init_command_hist_arr(&command_history);
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
	char* tokens[1000];

	int pipe_index = -1; 
	
	//placeholder for background so we know whether to call wait() or not
	int background = 0;
	
	print_prompt();	
	
	char* command = get_command();


	if(!command) return;

	add_to_history(&command_history,command);

	if(check_basic_commands(&command) == 0)
	{
		return;
	}
	input_parser = init_input_parser(command);

	free(command);

	int start_index = 0;

	char* token = get_token(input_parser);

	while(token != NULL)
	{
		tokens[start_index] = token;
		start_index++;
		token = get_token(input_parser);
	}
		
	if(strcmp(tokens[0],"fg") == 0)
	{
		tokens[start_index] = NULL;	
		bring_to_fg(tokens, &bg_proc_manager);
		free_tokens(tokens);
		free_input_parser(input_parser);
		return;
	}


	if(strcmp(tokens[start_index-1], "&") == 0)
	{
		background = 1;
	}
	
	//important: remove the background symbol and decrease start_index
	//so we don't segfault
	if(background)
	{
		free(tokens[start_index-1]);
		tokens[start_index-1] = NULL;
		start_index--;
		
	}
	else
	{
		//null terminate tokens, very important for execvp() call
		tokens[start_index] = NULL;
	}

	for(int i = 0; i < start_index;i++)
	{
		if(strcmp("|",tokens[i]) == 0)
		{
			if(i-1 >= 0 && strcmp("&",tokens[i-1]) == 0)
			{
				fprintf(stderr,"parse error near |: & \n");
				free_tokens(tokens);
				free_input_parser(input_parser);
				return;
			}
			pipe_index = i;
		}

	}
	
	//pipe symbol is present, handle accordingly 
	if(pipe_index != -1)
	{
		free(tokens[pipe_index]);
		tokens[pipe_index] = NULL;

		implement_pipeline(tokens,&tokens[pipe_index+1], background);

		free_tokens(tokens);
		free_tokens(&tokens[pipe_index+1]);
		free_input_parser(input_parser);
		return;

	}

	char** potential_files = check_for_files(tokens,start_index);

	char** final_command_array = prepare_command_array(tokens,start_index);

	
	//these next two lines may be a bit redundant
	//however, I think using exit() under an error was more
	//important so it was better to have a bit of 
	//duplicate code 
	if(execute_command(final_command_array,potential_files,background) == -1)
	{
		free_tokens(tokens);
		free(potential_files);
		free(final_command_array);
		free_input_parser(input_parser);
		free_history(&command_history);
		exit(EXIT_FAILURE);
	}

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
		kill(child_pid, SIGINT);
	}
}

/**
 * Function for reclaiming the zombies in the
 * background so they are removed from 
 * the process table
**/
void sig_chld_handler(int sig)
{
	int status;
	int pid;
	while((pid = waitpid(-1, &status,WNOHANG))> 0)
	{
		char buffer[100];
		snprintf(buffer, sizeof(buffer), "\npid %d done\n", pid);
		write(STDOUT_FILENO, buffer, strlen(buffer));		
		free_bg_proc(pid, &bg_proc_manager);
		print_prompt();
	}
}

/**
 * function used for the background child processes
 * exits if signal() fails
**/
void register_sig_chld_handler()
{
	if(signal(SIGCHLD,sig_chld_handler) == SIG_ERR)
	{
		perror("Could not register child handler signal");
		exit(EXIT_FAILURE);
	}
}

/**
 * Kills the child process by sending 
 * kill() signal, exits program if error
**/ 
void kill_child_process()
{
	if(kill(child_pid, SIGKILL) == -1) 
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
void implement_pipeline(char** first_command, char** second_command, int background)
{
	int fd[2];
	pid_t child_pid_1;
	pid_t child_pid_2;


	if(!first_command || !second_command)
	{
		perror("Pipe commands cannot be null");
		free(first_command);
		free(second_command);
		return;
	}


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

		int first_command_len = array_length(first_command);

		char** files = check_for_files(first_command,first_command_len);

		char** cleaned_array = prepare_command_array(first_command,first_command_len);

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
	
	close(fd[0]);
	close(fd[1]);

	if(!background)
	{
		waitpid(child_pid_1,NULL,0);
		waitpid(child_pid_2,NULL,0);
	}
	else
	{
		init_bg_process(child_pid_1,&command_history);
		init_bg_process(child_pid_2,&command_history);
	}

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

	close(fd);
}

/**
 * Function to change the output of a command, used
 * in redirection and pipes
 * @param file name where output will be sent
**/ 
void change_output(char* file_name)
{
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
	if(tokens == NULL)
	{
		printf("%s\n","Cannot clean redirection array: input NULL");
		return NULL;
	}

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
int execute_command(char** array,char** files, int background)
{

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

	if(child_pid == 0)
	{

		//we want to make sure control + c doesn't 
		//end a bg process
		if(background)
		{
			signal(SIGINT,SIG_IGN);
		}

		if(files[0] != NULL)
		{
			change_output(files[0]);
		}

		if(files[1] != NULL)
		{
			change_input(files[1]);
		}
		
		if(execvp(array[0],array) == -1)
		{
			perror("Could not execute command");
			return -1;
		}
	}
	
	if(!background)
	{
		waitpid(child_pid,NULL,0);
		child_pid = 0;
		return 0;
	}
	else 
	{
		init_bg_process(child_pid,&command_history);
	}
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
	char* input_file_name = NULL;
	char* output_file_name = NULL;

	if(array == NULL)
	{
		perror("Input array cannot be null");
		return NULL;
	}

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
 * Function just writes the prompt
 * out to stdout, makes code more modular
 * since we are calling it in a couple 
 * spots
 **/
void print_prompt()
{
	char* arrow = "enter command here: > ";

	if(write(STDOUT_FILENO,arrow,strlen(arrow)) == -1)
	{
		perror("Error writing to std out");
		exit(EXIT_FAILURE);
	}
}

/**
 * Function will initialize a bg process 
 * and place it into our bg processes array
 * by finding the first available non-null index
**/
void init_bg_process(pid_t process_id,command_history_t* command_history)
{
	if(process_id == -1 || command_history == NULL)
	{
		fprintf(stderr,"process id must not be -1 and command history must not be null");
		return; 
	}

	process_t* bg_process = malloc(sizeof(process_t));

	if(!bg_process)
	{
		perror("Malloc failure trying to init bg process");
		exit(EXIT_FAILURE);
	}
    bg_process->command = command_history->commands[command_history->last_used_index-1];
	bg_process->pid = process_id;


	for(int i = 0; i < MAX_BG_PROC; i++)
	{
		if(bg_proc_manager.bg_processes[i] == NULL)
		{
			bg_proc_manager.bg_processes[i] = bg_process;
			bg_process->index = i;
			bg_proc_manager.size++;
			break;
		}
	}
	printf("[%d] %d %s\n", bg_process->index+1,bg_process->pid, bg_process->command);	
	return;
}

/**
 * Function frees the bg process
 * by finding the job with that pid
 * this also allows us to simply assign 
 * the next bg job to the first available space
 **/ 
void free_bg_proc(pid_t pid, bg_proc_manager_t* bg_proc_manager)
{
	if(!bg_proc_manager)
	{
		fprintf(stderr, "cannot free bg process if bg process manager is null");
		return;
	}
	for(int i = 0; i < MAX_BG_PROC; i++)
	{
		if(bg_proc_manager->bg_processes[i] != NULL && bg_proc_manager->bg_processes[i]->pid == pid)
		{
			free(bg_proc_manager->bg_processes[i]);
			bg_proc_manager->bg_processes[i] = NULL;
			bg_proc_manager->size--;
			break;
		}
	}
}

/**
 * Function mimics the Unix "fg" command
 * this command can have an index number passed in or
 * not, if it doesn't have an index
 * we pull the last started bg process
 * to the fg
 **/
void bring_to_fg(char** tokens, bg_proc_manager_t* bg_proc_manager)
{
	if(!(*tokens))
	{
		fprintf(stderr, "Cannot move process to foreground, tokens is NULL");
		return;
	}

	int tokens_length = array_length(tokens);

	// handle the case where no index is passed in
	if(tokens_length == 1)
	{
		int index = -1;

		for(int i = 0; i < MAX_BG_PROC; i++)
		{
			if(bg_proc_manager->bg_processes[i] != NULL)
			{
					index = i;
			}
		}

		if(index != -1)
		{
			waitpid(bg_proc_manager->bg_processes[index]->pid,NULL,0);
			free_bg_proc(bg_proc_manager->bg_processes[index]->pid, bg_proc_manager);
			bg_proc_manager->size--;
		}
		else
		{
			printf("%s\n", "No bg processes currently running");
			return;
		}
			
	}
	else
	{
		if(tokens_length > 2)
		{
			printf("%s\n", "Please provide a single valid bg process index");
			return;
		}

		//atoi returning 0 either means the input
		//was not valid
		//or it means that the index
		//passed in was 0 which is still invalid
		int bg_index = atoi(tokens[1]);
		if(bg_index-1 < 0 || bg_index-1 >= MAX_BG_PROC)
		{
			printf("%s\n", "no such job");
			return;
		}
		
		if(bg_proc_manager->bg_processes[bg_index-1] != NULL)
		{
			waitpid(bg_proc_manager->bg_processes[bg_index-1]->pid,NULL,0);
			free_bg_proc(bg_proc_manager->bg_processes[bg_index-1]->pid, bg_proc_manager);
			bg_proc_manager->size--;
		}
		else
		{
			printf("%s\n", "no such job");
			return;
		}
	}
}

/**
 * Function is a semi-copy of the unix "jobs"
 * command
**/
void print_jobs(bg_proc_manager_t* bg_proc_manager)
{
	if(bg_proc_manager->size == 0)
	{
		printf("%s\n", "no jobs running in background");
		return;
	}

	printf("%s\n","No.\tStatus\tCommand");
	for(int i = 0; i < MAX_BG_PROC; i++)
	{
		if(bg_proc_manager->bg_processes[i] != NULL)
		{
			printf("[%d]\tRunning\t%s\n",i+1, bg_proc_manager->bg_processes[i]->command);
		}
	}
	fflush(stdout);
}

/**
 * Function initializes the bg process manager which
 * will hold the array and keep track of number 
 * of current bg processes
**/
void init_bg_proc_manager(bg_proc_manager_t* process_manager)
{
	if(process_manager == NULL)
	{
		fprintf(stderr, "background process manager cannot be NULL");
		return;
	}
	for(int i = 0; i < MAX_BG_PROC; i++)
	{
		process_manager->bg_processes[i] = NULL;
	}
	process_manager->size = 0;
}

void init_command_hist_arr(command_history_t* command_history)
{
	for(int i = 0; i < MAX_COM_HIST; i++)
	{
		command_history->commands[i] = NULL;
	}
	command_history->size = 0;
	command_history->last_used_index = 0;
}

/**
 * Allows the user to see their command history
**/
void print_history(command_history_t *command_history)
{
	if(!command_history)
	{
		fprintf(stderr,"cannot print command history if command_history struct is NULL\n");
		return;
	}
	for(int i = 0; i < command_history->size; i++)
	{
		printf("%d %s\n", i+1, command_history->commands[i]);
		fflush(stdout);
	}
}

/**
 * This function will add to our global command
 * history variable so that users can see their
 * command history
 * if the user is at the max history, it evicts
 * the last history entry
**/
void add_to_history(command_history_t *command_history, char *command)
{
	if(!command_history || !command)
	{
		fprintf(stderr, "command_history and command cannot be NULL");
		return;
	}

	char* hist_command = strdup(command);
	if(command_history->size == MAX_COM_HIST)
	{
		free(command_history->commands[command_history->last_used_index]);
		command_history->commands[command_history->last_used_index] = NULL;
		command_history->commands[command_history->last_used_index] = hist_command;
		return;
	}	
		command_history->commands[command_history->last_used_index] = hist_command;
		if(command_history->last_used_index < MAX_COM_HIST-1)
		{
			command_history->last_used_index++;
		}
		command_history->size++;
		return;
}

/**
 * cleans up the command_history global variable
**/
void free_history(command_history_t *command_history)
{
	if(!command_history)
	{
		fprintf(stderr, "Cannot free command history if it is NULL");
		return;
	}

	for(int i = 0; i < command_history->size;i++)
	{
		if(command_history->commands[i] != NULL)
		{
			free(command_history->commands[i]);
			command_history->commands[i] = NULL;
		}
	}
}

/**
 * this function serves as a way to clean up the run_shell()
 * function, this is where I will add commands that don't
 * necessarily need tokenizing
**/
int check_basic_commands(char** command)
{
	if(!(*command))
	{
		fprintf(stderr, "Cannot interpret NULL command");
		return 1;
	}

	if(strcmp(*command,"exit") == 0)
	{
		free(*command);
		free_history(&command_history);
		exit(EXIT_SUCCESS);
	}

	else if(strcmp(*command,"jobs") == 0)
	{
		print_jobs(&bg_proc_manager);
		free(*command);
		return 0;
	}

	else if(strcmp(*command, "history") == 0)
	{
		print_history(&command_history);
		free(*command);
		return 0;
	}

	return 1;
}
