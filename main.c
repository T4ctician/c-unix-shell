#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h> 
#include <glob.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <errno.h>
#include "parser.h"

//Global Variable
pid_t child_pid = 0;

// Forward declarations
void execmd(command* cmd);
void executeCommand(command **cmd_line);
void set_prompt(char *new_prompt, char **prompt, const char *default_prompt);
void signal_handler(int signal_number);
void sigchld_handler(int signo);
void setup_sigchld_handler();
void executePipeline(command **pipeline, int num_cmds, int background);
void execute_history_command(const char *line);
void handle_history_command(const char *line);
char* expand_environment_variables(char* input);

int main() {
    char *line;
    char *default_prompt = strdup("default% ");
    char *current_prompt = strdup(default_prompt); // Default prompt
    command **cmd_line;

    // Set up the signal handler for SIGCHLD to handle zombie processes
    setup_sigchld_handler();

    struct sigaction sa;
    sa.sa_handler = signal_handler; // Set the handler function
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Ignore CTRL-C (SIGINT), CTRL-\ (SIGQUIT), CTRL-Z (SIGTSTP)
    sigaction(SIGINT, &sa, NULL);  
    sigaction(SIGQUIT, &sa, NULL); 
    sigaction(SIGTSTP, &sa, NULL); 

    while (1) {
        line = readline(current_prompt);

        //CTRL D
        if (line == NULL) {
            if (isatty(STDIN_FILENO)){
                printf("\nCTRL-D pressed. Type 'exit' to quit shell.\n");
                continue;
            }
        }

        //EOF
        if (line == NULL) {
                break;
        }

        // Check for prompt change command and handle it
        if (strncmp(line, "prompt ", 7) == 0)
        {
            set_prompt(line + 7, &current_prompt, default_prompt);
            continue;
        }

        // Handle history command
        if (strcmp(line, "history") == 0) {
            handle_history_command(line);
            free(line);
            continue;
        }

        // Execute a command from history
        if (line[0] == '!') {
            execute_history_command(line);
            free(line);
            continue;
        }

        // If the line is not empty, execute the commands
        if (line && *line) {
            char* expanded_line = expand_environment_variables(line);
            free(line); // Free the original line
            line = expanded_line; // Use the expanded line for further processing

            add_history(line); // add readline's history feature
            cmd_line = process_cmd_line(line, 1); // Parse the command line into an array of command structures
            executeCommand(cmd_line); // Execute parsed commands
            clean_up(cmd_line); // Clean up memory
            free(line); // Free the input line
        } else {
            free(line); // Free the input line if it's empty
        }
    }

    free(current_prompt); // Free the prompt memory before exiting
    free(default_prompt); // Free the default prompt memory before exiting

    return 0;
}

// Function to change the shell prompt dynamically
void set_prompt(char *new_prompt, char **prompt, const char *default_prompt){
    if (new_prompt == NULL || *new_prompt == '\0' || isspace((unsigned char)*new_prompt))
    {
        // If new_prompt is NULL or an empty string, set back to default prompt
        if (*prompt != NULL)
        {
            free(*prompt);
            *prompt = strdup(default_prompt);
        }
        return;
    }

    if (*prompt != NULL)
    {
        free(*prompt);
    }

    // Allocate memory for new prompt with extra space for a space and null terminator
    *prompt = malloc(strlen(new_prompt) + 2);
    if (*prompt != NULL)
    {
        // Copy new prompt and append a space and null terminator
        snprintf(*prompt, strlen(new_prompt) + 2, "%s ", new_prompt);
        printf("Setting prompt to: %s\n", *prompt);
    }
}

// Built-in 'pwd' command implementation
void builtin_pwd() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
}

// Built-in 'cd' command implementation
void builtin_cd(char *path) {
    if (chdir(path) != 0) {
        perror("cd");
    }
}

// Signal handler for SIGINT, SIGQUIT, and SIGTSTP
void signal_handler(int signal_number) {
    const char *message;
    switch (signal_number) {
        case SIGINT:
            message = "\nCTRL-C pressed. Type 'exit' to quit shell.\n";
            break;
        case SIGQUIT:
            message = "\nCTRL-\\ pressed. Type 'exit' to quit shell.\n";
            break;
        case SIGTSTP:
            message = "\nCTRL-Z pressed. Type 'exit' to quit shell.\n";
            break;
        default:
            message = "\nSignal caught.\n";
            break;
    }
    // Safe to re-establish the signal handler here
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Automatically restart system calls if possible
    sigaction(signal_number, &sa, NULL);

    // Write the message to stdout and re-display the prompt
    write(STDOUT_FILENO, message, strlen(message));
}

void sigchld_handler(int signo) {
    (void)signo; // Unused parameter
    int status;
    pid_t pid;

    // Wait for all children without blocking
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Reap zombie processes
        if (WIFEXITED(status)) {
            printf("[Background job with PID %d finished with exit code %d]\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[Background job with PID %d finished due to signal %d]\n", pid, WTERMSIG(status));
        }
    }
}

void alarm_handler(int signum)
{
    (void)signum;
    fflush(stdout);

    if (child_pid != 0) {
        kill(child_pid, SIGKILL);
        printf("System call is taking too long. Terminating child process...\n\n");
        fflush(stdout);
    }
}


void setup_sigchld_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}


// Built-in wild card function
void expand_wildcards(command* cmd) {
    glob_t glob_result;
    char **new_argv = NULL;
    int new_argc = 0;
    size_t alloc_size = 0;

    for (int i = 0; cmd->argv[i] != NULL; i++) {
        // Initialize glob_result
        memset(&glob_result, 0, sizeof(glob_result));
        // Use GLOB_NOCHECK to ensure non-matching patterns are returned
        if (glob(cmd->argv[i], GLOB_NOCHECK | GLOB_TILDE | GLOB_APPEND, NULL, &glob_result) == 0) {
            alloc_size += sizeof(char*) * (glob_result.gl_pathc + 1);
            new_argv = realloc(new_argv, alloc_size);
            for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                new_argv[new_argc++] = strdup(glob_result.gl_pathv[j]);
            }
        }
        globfree(&glob_result);
    }
    new_argv[new_argc] = NULL; // Terminate the new argv with NULL

    // Free the old argv
    for (int i = 0; cmd->argv[i] != NULL; i++) {
        free(cmd->argv[i]);
    }
    free(cmd->argv);

    // Assign the new argv to the command
    cmd->argv = new_argv;
}

// Function to execute built-in or external commands
void executeCommand(command **cmd_line) {
    int i = 0;

    while (cmd_line[i] != NULL) {
        int background = cmd_line[i]->background;
        int num_cmds = 1;

        if (cmd_line[i]->pipe_to) {
            // Count the number of commands in the pipeline
            while (cmd_line[i + num_cmds] && cmd_line[i + num_cmds]->pipe_to) {
                num_cmds++;
            }

            // Execute the pipeline
            executePipeline(&cmd_line[i], num_cmds + 1, background);

            i += num_cmds + 1;
        } else if (strcmp(cmd_line[i]->com_name, "exit") == 0) {
            // Handle 'exit' built-in command
            exit(0);
        } else if (strcmp(cmd_line[i]->com_name, "pwd") == 0) {
            // Handle 'pwd' built-in command
            builtin_pwd();
            i++;
        } else if (strcmp(cmd_line[i]->com_name, "cd") == 0) {
            // Handle 'cd' built-in command
            char *current_dir = getcwd(NULL, 0); // Get the current working directory

            // If no argument is given, or if it is "~", change to the home directory
            if (cmd_line[i]->argv[1] == NULL || strcmp(cmd_line[i]->argv[1], "~") == 0) {
                builtin_cd(getenv("HOME"));
            }
            else if (strcmp(cmd_line[i]->argv[1], "-") == 0) {
                // Change to the last directory
                char *last_dir = getenv("OLDPWD");
                if (last_dir != NULL) {
                    builtin_cd(last_dir);
                } else {
                    printf("cd: OLDPWD not set\n");
                }
            }
            else if (cmd_line[i]->argv[1][0] == '~' && cmd_line[i]->argv[1][1] == '/') {
                // Change to a subdirectory of the home directory
                char *home_dir = getenv("HOME");
                char *subdir_path = malloc(strlen(home_dir) + strlen(cmd_line[i]->argv[1]) - 1); // -1 to exclude '~'
                strcpy(subdir_path, home_dir);
                strcat(subdir_path, cmd_line[i]->argv[1] + 1); // Skip the '~' character

                builtin_cd(subdir_path);

                free(subdir_path);
            } else {
                // Change to the directory specified by the argument
                builtin_cd(cmd_line[i]->argv[1]);
            }

            // Update the "OLDPWD" environment variable
            setenv("OLDPWD", current_dir, 1);

            // Free the memory allocated by getcwd
            free(current_dir);

            i++;
        } else {
            // Execute a single external command using execmd
            execmd(cmd_line[i]);
            i++;
        }
    }
}

void execmd(command *cmd)
{
    expand_wildcards(cmd);
    // Check if the command should run in the background
    int background = cmd->background;
    (void)background;

    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return;
    }

    if (pid == 0)
    { // Child process
        

        // Handle input redirection
        if (cmd->redirect_in != NULL)
        {
            int in_fd = open(cmd->redirect_in, O_RDONLY);
            if (in_fd < 0)
            {
                perror("open input redirection");
                exit(EXIT_FAILURE);
            }
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }

        // Handle output redirection
        if (cmd->redirect_out != NULL)
        {
            int out_fd = open(cmd->redirect_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out_fd < 0)
            {
                perror("open output redirection");
                exit(EXIT_FAILURE);
            }
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }

        // Handle error redirection
        if (cmd->redirect_err != NULL)
        {
            int err_fd = open(cmd->redirect_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (err_fd < 0)
            {
                perror("open error redirection");
                exit(EXIT_FAILURE);
            }
            dup2(err_fd, STDERR_FILENO);
            close(err_fd);
        }

        // Execute the command
        if (execvp(cmd->com_name, cmd->argv) == -1)
        {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }
    else if (pid > 0)
    {
        // Parent process
        child_pid = pid;
        signal(SIGALRM, alarm_handler);
        alarm(60); // Set timer for 5 seconds
        
        if (!cmd->background)
        {
            int status;
            waitpid(pid, &status, 0);
            alarm(0); // Cancel the alarm
        }
        else
        {
            printf("[Started background job with PID %d]\n", pid);
        }
    }
    else
    {
        perror("fork");
    }
}

void executePipeline(command **pipeline, int num_cmds, int background) {
    int pipefds[num_cmds - 1][2]; // Array to hold the pipe file descriptors
    pid_t pids[num_cmds];

    if (num_cmds == 0) {
        printf("Empty command.\n");
        return; // return to program
    }

    // Set up all the necessary pipes in advance
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipefds[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();
        if (pids[i] == 0) { // Child process
            // Handle input from the previous command, if not the first command
            if (i > 0) {
                dup2(pipefds[i - 1][0], STDIN_FILENO);
                close(pipefds[i - 1][1]); // Close write end of the previous output pipe
            } else if (pipeline[i]->redirect_in != NULL) {
                // Open the input file and use a temporary file descriptor
                int in_fd = open(pipeline[i]->redirect_in, O_RDONLY);
                if (in_fd < 0) {
                    perror("open input redirection");
                    exit(EXIT_FAILURE);
                }

                int temp_in_fd = dup(STDIN_FILENO); // Duplicate standard input
                dup2(in_fd, STDIN_FILENO); // Redirect input from the file
                close(in_fd);

                // Restore standard input after redirection
                dup2(temp_in_fd, STDIN_FILENO);
                close(temp_in_fd);
            }

            // Close unused pipe file descriptors in the child process
            for (int j = 0; j < num_cmds - 1; j++) {
                if (j != i) {
                    close(pipefds[j][0]);
                    close(pipefds[j][1]);
                }
            }

            // Handle output to the next command, if not the last command
            if (i < num_cmds - 1) {
                close(pipefds[i][0]); // Close the read end of the pipe in the child process
                dup2(pipefds[i][1], STDOUT_FILENO);
            }

            // Check for redirection and call execmd if needed
            if (pipeline[i]->redirect_out != NULL || pipeline[i]->redirect_err != NULL) {
                execmd(pipeline[i]);
            } else {
                // Execute the command
                execvp(pipeline[i]->com_name, pipeline[i]->argv);
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else if (pids[i] < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
    }

    // Parent process closes all pipe file descriptors
    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    // Wait for all child processes if not in the background
    if (!background) {
        int status;
        for (int i = 0; i < num_cmds; i++) {
            waitpid(pids[i], &status, 0);
        }
    } else {
        // For background processes print their PIDs 
        for (int i = 0; i < num_cmds; i++) {
            printf("[Started background job with PID %d]\n", pids[i]);
        }
    }
}

// Function to handle 'history' built-in command
void handle_history_command(const char *line __attribute__((unused))) {
    HIST_ENTRY **the_history_list = history_list();
    if (the_history_list) {
        for (int i = 0; the_history_list[i]; i++) {
            printf("%d: %s\n", i + history_base, the_history_list[i]->line);
        }
    }
}

// Function to handle history selection
void execute_history_command(const char *line) {
    // Handle '!' without a number or string following it
    if (line[1] == '\0') {
        // Perhaps repeat the last command or show an error message
        printf("Error: '!' requires a command number or prefix string.\n");
        return;
    }

    // Handle '!' followed by a number
    if (isdigit(line[1])) {
        int cmd_number = atoi(&line[1] - 1 );
        HIST_ENTRY *hist_entry = history_get(history_base + cmd_number);
        if (hist_entry && hist_entry->line) {
            printf("%s\n", hist_entry->line);
            command **cmd_line = process_cmd_line(hist_entry->line, 1); // Parse the command line
            if (cmd_line) {
                executeCommand(cmd_line); // Execute the parsed command
                clean_up(cmd_line); // Clean up after execution
            }
        } else {
            printf("No such command in history.\n");
        }
    } else { // Handle '!' followed by a string
        int offset = history_search_prefix(&line[1], -1);
        if (offset != -1) {
            HIST_ENTRY *hist_entry = history_get(history_base + offset);
            if (hist_entry && hist_entry->line) {
                printf("%s\n", hist_entry->line);
                command **cmd_line = process_cmd_line(hist_entry->line, 1); // Parse the command line
                if (cmd_line) {
                    executeCommand(cmd_line); // Execute the parsed command
                    clean_up(cmd_line); // Clean up after execution
                }
            } else {
                printf("No such command in history.\n");
            }
        } else {
            printf("No such command in history.\n");
        }
    }
}

// Functions to handle environment
char* expand_environment_variables(char* input) {
    char* expanded_input = strdup(input); // Duplicate the input to avoid modifying the original
    char* start = expanded_input;
    while ((start = strchr(start, '$')) != NULL) { // Find the '$' symbol
        char* end = start + 1;
        while (isalnum(*end) || *end == '_') end++; // Find the end of the variable name
        size_t var_name_length = end - (start + 1);
        if (var_name_length > 0) {
            char* var_name = strndup(start + 1, var_name_length);
            char* var_value = getenv(var_name); // Get the value from the environment
            if (var_value) {
                // Replace the variable in the string with its value
                size_t expanded_input_length = strlen(expanded_input);
                char* new_expanded_input = malloc(expanded_input_length - var_name_length + strlen(var_value) + 1);
                strncpy(new_expanded_input, expanded_input, start - expanded_input);
                strcpy(new_expanded_input + (start - expanded_input), var_value);
                strcpy(new_expanded_input + (start - expanded_input) + strlen(var_value), end);
                free(expanded_input);
                expanded_input = new_expanded_input;
                start = expanded_input + (start - expanded_input) + strlen(var_value); // Move past the replaced value
            } else {
                start = end; // Move past the variable name
            }
            free(var_name);
        } else {
            start = end; // Move past the '$'
        }
    }
    return expanded_input;
}

