//#####################################################
//                       _          _ _
//                      | |        | | |
//    ___  ___  __ _ ___| |__   ___| | |
//   / __|/ _ \/ _` / __| '_ \ / _ \ | |
//   \__ \  __/ (_| \__ \ | | |  __/ | |
//   |___/\___|\__,_|___/_| |_|\___|_|_|
//   
// AUTHOR: Jeremy Butt (201527710) ©2020
//
// Implemented Functionality:
//      -> Cancellation
//      -> History
//      -> Pipes
//      -> Redirection
//
//#####################################################

#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>

#define BUFFERSIZE 64
#define DELIMITERS " \t\r\n\a"

// Global Vars
int parent_pid;
int num_children;
int history_fd;

// Helper Functions
/**
 * printDir()
 * 
 * - prints current working directory
 */
void printDir() 
{ 
    char cwd[1024]; 
    getcwd(cwd, sizeof(cwd)); 
    printf("\n%s", cwd); 
}

/**
 * is_mem_valid()
 * 
 * - helper function for seeing if memory is valid.
 * @returns bool of mem being valid.
 */
bool is_mem_valid(void* mem)
{
    if(!mem)
    {
        fprintf(stderr, "seashell: Memory Not Valid. Possible allocation error.\n");
        exit(EXIT_FAILURE);
    }
    return true;
}

void sig_int_handler()
{
    if (getpid() != parent_pid)
    {
        exit(128 + SIGINT);
    }
    else if (num_children == 0)
    {
        exit(128 + SIGINT);
    }
}

/**
 * init_history()
 * 
 * - opens a fresh file for the recording of user commands in the current session.
 * @returns file descriptor to the history file.
 */
int init_history()
{
    char *history_path = strcat(getenv("HOME"), "/.seashell_history");
    remove(history_path);
    int fd = open(history_path, O_RDWR | O_CREAT, 0640);
    if (fd == -1)
    {
        err(-1, "failed to open or create ~/.seashell_history\n");
    }
    return fd;
}

/**
 * write_cmds_to_history()
 * 
 * - writes the command in cmd to the history file (history_fd).
 * @returns 0 on success.
 */
int write_cmds_to_history(char* cmd, int size)
{
    if (write(history_fd, cmd, size) == -1)
    {
        err(-1, "unable to write to ~/.seashell_history\n");
    }
    if (write(history_fd, "\n", 1) == -1)
    {
        err(-1, "Unable to write newline to ~/.seashell_history\n");
    }
    return 0;
}

// Built-in Shell Function
/**
 * cd()
 * 
 * - change working directory.
 * @returns 0 on success.
 */
int cd(char** args)
{
    if (args[1] == NULL) 
    {
        fprintf(stderr, "seashell: command \"cd\" expects argument \n");
    } 
    else 
    {
        if (chdir(args[1]) != 0) 
        {
            perror("seashell: unable to cd...");
        }
    }
    return 0;
}

/**
 * info()
 * 
 * - prints out logo and info.
 * @returns 0 on success.
 */
int info()
{
    printf("########################################################\n");
    printf("\n");
    printf("           _.-''|''-._\n");
    printf("        .-'     |     `-.\n");
    printf("      .'\\       |       /`.\n");
    printf("    .'   \\      |      /   `.\n");
    printf("    \\     \\     |     /     /\n");
    printf("     `\\    \\    |    /    /'\n");
    printf("       `\\   \\   |   /   /'\n");
    printf("         `\\  \\  |  /  /'\n");
    printf("        _.-`\\ \\ | / /'-._\n");
    printf("       {_____`\\|//'_____}\n");
    printf("               `-'\n");
    printf("\n");
    printf("                    _          _ _ \n");
    printf("                   | |        | | |\n");
    printf(" ___  ___  __ _ ___| |__   ___| | |\n");
    printf("/ __|/ _ \\/ _` / __| '_ \\ / _ \\ | |\n");
    printf("\\__ \\  __/ (_| \\__ \\ | | |  __/ | |\n");
    printf("|___/\\___|\\__,_|___/_| |_|\\___|_|_|\n");
    printf("\n");
    printf("Author: Jeremy Butt ©2020\n");
    printf("########################################################\n");
    char* username = getenv("USER"); 
    printf("USER is: @%s\n", username);
    printf("########################################################\n");
    char cwd[1024]; 
    getcwd(cwd, sizeof(cwd)); 
    printf("CWD: %s\n", cwd);
    printf("########################################################\n");
    return 0;
}

/**
 * history()
 * 
 * - prints out contents of the history file (history_fd)
 * @returns 0 on success.
 */
int history()
{
    char* s;
    int offset = 0;
    
    while (pread(history_fd, &s, sizeof(char), offset) > 0)
    {
        offset += 1;
        if ((write(STDOUT_FILENO, &s, 1)) < 0)
        {
            err(-1, "Unable to write to stdout\n");
        }
    }
    return 0;
}

/**
 * exit()
 * 
 * - exits the shell
 */
int f_exit()
{
    exit(EXIT_SUCCESS);
}

// Shell Functions
/**
 * get_cmds()
 * 
 * - collect commands from stdin and tokenize and parse.
 * @returns char*** tokenized_cmds --> array of cmds in the form of cmds[cmd #][argument #]
 * int* num_cmds is returned by reference and is the number of cmds
 */
char*** get_cmds(int* num_cmds)
{
    // get full line from stdin
    char* line = NULL;
    size_t line_buffer_size = 0;
    getline(&line, &line_buffer_size, stdin);

    write_cmds_to_history(line, line_buffer_size);

    // make array for cmds split by pipes
    size_t token_buffer_size = BUFFERSIZE;
    char** cmds = (char**)calloc(token_buffer_size, sizeof(char*));
    is_mem_valid((void*)cmds);

    // split cmds by | and store in cmds
    char* cmd = strtok(line, "|");
    int i = 0;
    while (cmd != NULL)
    {
        cmds[i] = cmd;
        i++;
        
        // if need more space, reallocate
        if (i > token_buffer_size - 1)
        {
            token_buffer_size += BUFFERSIZE;
            cmds = (char**)realloc(cmds, token_buffer_size * sizeof(char*));
            is_mem_valid((void*)cmds);
        }
        cmd = strtok(NULL, "|");
    }
    cmds[i] = NULL;
    *num_cmds = i;

    // make 2d array for tokenized cmds 
    char*** tokenized_cmds = (char***)calloc(token_buffer_size, sizeof(char**));
    is_mem_valid((void*)tokenized_cmds);

    for(int i = 0; i < *num_cmds; i++)
    {
        // allocate space for tokens
        char** tokens = (char**)calloc(token_buffer_size, sizeof(char*));
        is_mem_valid((void*)tokens);

        //tokenize the cmds and store in tokens[] which is then stored in tokenized_cmds
        char* line = cmds[i];
        char* token = strtok(line, DELIMITERS);
        int j = 0;
        while (token != NULL)
        {
            tokens[j] = token;
            j++;

            // if need more space, reallocate
            if (j > token_buffer_size - 1)
            {
                token_buffer_size += BUFFERSIZE;
                tokens = (char**)realloc(tokens, token_buffer_size * sizeof(char*));
                is_mem_valid((void*)tokens);
            }

            token = strtok(NULL, DELIMITERS);
        }
        tokens[j] = NULL;
        tokenized_cmds[i] = tokens;
    }
    return tokenized_cmds;
}

/**
 * execute()
 * 
 * - sets up file descriptors and then forks and execvps commands from cmds[][]
 * @returns 0 on success.
 */
int execute(int n, char*** cmds)
{
    int READ = 0;
    int WRITE = 1;

    int pd[2];
    int fd[2];
    int f_in = 0;
    int i = 0;

    // check for built-in functionality
    if(strcmp(cmds[i][0], "cd") == 0)
    {
        return cd(cmds[i]);
    }
    else if(strcmp(cmds[i][0], "info") == 0)
    {
        return info();
    }
    else if(strcmp(cmds[i][0], "history") == 0)
    {
        return history();
    }
    else if(strcmp(cmds[i][0], "exit") == 0)
    {
        return f_exit();
    }

    // for every command split by pipes, we need to fork and exec
    while(i < n)
    {
        pid_t pid;
        int status;
        pipe(fd);
        pid = fork();

        // parent process
        if(pid != 0) 
        {
            waitpid(pid, &status, 0);
            close(fd[WRITE]);
            num_children--;

            if(WIFEXITED(status))
            {
                if(WEXITSTATUS(status) == 0)
                {
                    if(i == n-1)
                    {
                        printf("The program exited with code 0\n");
                    }
                } 
                else if(WEXITSTATUS(status) == 255) 
                {
                    printf("The program %s does not exist \n", cmds[i][0]);
                } 
                else 
                {
                    printf("ERROR: Error code: %d", WEXITSTATUS(status));
                }
            }
            else
            {
                printf("UNKNOWN: exited with code %d", status);
            }

            // store current fd for reading for the next childs input.
            f_in = fd[READ];
        }
        else // child process
        {
            num_children++;
            dup2(f_in, 0);
            if(i != n-1) 
            {
                dup2(fd[WRITE], 1);
            }
            close(fd[READ]);

            // handle redirection of streams
            int stream_fd;
            int j = 0;
            char* cmd = cmds[i][j];
            while (cmd != NULL)
            {
                if(strcmp(cmd, ">") == 0)
                {
                    int stream_fd = open(cmds[i][j+1], O_RDWR | O_CREAT | O_APPEND, 0640);
                    dup2(stream_fd, 1);
                    cmds[i][j+1] = 0; // null out stream director and file
                    cmds[i][j] = 0;

                }
                if(strcmp(cmd, "2>") == 0)
                {
                    int stream_fd = open(cmds[i][j+1], O_RDWR | O_CREAT | O_APPEND, 0640);
                    dup2(stream_fd, 2);
                    cmds[i][j+1] = 0; // null out stream director and file
                    cmds[i][j] = 0;
                }
                if(strcmp(cmd, "<") == 0)
                {
                    int stream_fd = open(cmds[i][j+1], O_RDWR | O_CREAT | O_APPEND, 0640);
                    dup2(stream_fd, 0);
                    cmds[i][j+1] = 0; // null out stream director and file
                    cmds[i][j] = 0;
                }
                j++;
                cmd = cmds[i][j];
            }

            // execute the command with arguments.
            exit(execvp(cmds[i][0], cmds[i]));

            return 0;
        }
        i++;
    }

    return 0;
}


int main(int argc, char **argv)
{
    parent_pid = getpid();
    signal(SIGINT, sig_int_handler); //signal handler for ctrl-c

    history_fd = init_history();

    char*** cmds;
    int num_cmds = 0;
    int ret_status = 0;

    info();

    while (1)
    {
        printDir();
        printf("$ ");
        cmds = get_cmds(&num_cmds);
        execute(num_cmds, cmds);
    }
}
