#ifndef SHELL_H
#define SHELL_H
#include <sys/types.h>

#define MAX_BG_PROC 100
#define MAX_COM_HIST 200

typedef struct process_t
{
	int index;
	pid_t pid;
	char* command;

}process_t;

typedef struct bg_proc_manager_t
{
	process_t* bg_processes[MAX_BG_PROC];
	int size;
	
}bg_proc_manager_t;

typedef struct command_history_t
{
	char* commands[MAX_COM_HIST];
	int last_used_index;
	int size; 
}command_history_t;

void run_shell();

void register_signal_handler();

void kill_child_process();

void sig_int_handler(int handler);

void change_output(char* file_name);

void change_input(char* file_name);

void register_sig_chld_handler();

void sig_chld_handler(int sig);

int execute_command(char** command, char** files, int background);

char** prepare_command_array(char** tokens, int array_length);

char** check_for_files(char** array, int array_length);

void implement_pipeline(char** first_command,char** second_command, int background);

void print_prompt();

void init_bg_process(pid_t pid,command_history_t* command_history);

void free_bg_proc(pid_t pid,bg_proc_manager_t* bg_proc_manager);

void bring_to_fg(char** tokens, bg_proc_manager_t* bg_proc_manager);

void print_jobs(bg_proc_manager_t* bg_proc_manager);

void init_bg_proc_manager(bg_proc_manager_t* bg_proc_manager);

void init_command_hist_arr(command_history_t* command_history);

void print_history(command_history_t* command_history);

void add_to_history(command_history_t* command_history, char* command);

void free_history(command_history_t* command_history);

int check_basic_commands(char** command);

#endif

