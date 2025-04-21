// Author: Daniel Schuster
// Code template provided by Professor Jesse Chaney at Portland State University
/*
This program is an interactive shell that supports operation of linux built-in
commands (ls, cat, etc.), and my implementation of the following:
command history via "history" 
display current directory via "cwd"
change directory via "cd"
echo text via "echo"
exiting this shell via "bye" (using "exit" will exit the outer shell this shell runs in)
piping of an arbitrary number of commands via "|"
redirection of input via "<" and output via ">"
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/wait.h>

#include "psush.h"

#define PROMPT_LEN 100
#define HOSTNAME_LEN 50
#define HIST 15
#define READ 0
#define WRITE 1


//globals
unsigned short is_verbose = 0;
char *hist[HIST] = {0};
pid_t child_pid = 0;

int 
main( int argc, char *argv[] )
{
    int ret = 0;

    if (signal(SIGINT, signal_handler) == SIG_ERR)
      printf("failed to catch SIGINT signal\n");

    memset(hist, 0, sizeof(hist));

    simple_argv(argc, argv);
    ret = process_user_input_simple();

    //free the command history memory
    for (int i = 0; i < HIST; ++i)
       if (hist[i]) free(hist[i]);
    return ret;
}

int 
process_user_input_simple(void)
{
    char str[MAX_STR_LEN] = {'\0'};
    char *ret_val = NULL;
    char *raw_cmd = NULL;
    cmd_list_t *cmd_list = NULL;
    int cmd_count = 0;
    char prompt[PROMPT_LEN] = {'\0'};
    char hostname[HOSTNAME_LEN] = {'\0'};
    char *cwd = NULL;
    int saved_stdin = 0;
    int saved_stdout = 0;

    for ( ; ; ) {
        //create user prompt
        gethostname(hostname, HOSTNAME_LEN);
        getcwd(str, MAX_STR_LEN); //use str as buffer
        cwd = strdup(str);
        sprintf(prompt, " %s %s\n%s@%s # ", PROMPT_STR, cwd, getenv("USER"), hostname);
        if (cwd) free(cwd);

        //only display prompt if output device is a tty (terminal)
        if (isatty(STDOUT_FILENO))
           fputs(prompt, stdout);
        memset(str, 0, MAX_STR_LEN);
        ret_val = fgets(str, MAX_STR_LEN, stdin);

        if (NULL == ret_val) {
            // end of input, a control-D was pressed.
            // Bust out of the input loop and go home.
            break;
        }

        // STOMP on the pesky trailing newline returned from fgets().
        if (str[strlen(str) - 1] == '\n') {
            // replace the newline with a NULL
            str[strlen(str) - 1] = '\0';
        }
        if (strlen(str) == 0) {
            // An empty command line.
            // Just jump back to the promt and fgets().
            continue;
        }

        if (strcmp(str, BYE_CMD) == 0) {
            // Pickup your toys and go home. I just hope there are not
            // any memory leaks. ;-)
            break;
        }

        //update history
        if (HIST - 1) free(hist[HIST - 1]); //free oldest item
        for (int i = HIST - 2; i >= 0; --i) //shift by 1
           hist[i + 1] = hist[i];
        hist[0] = strdup(str); //add current command


        // Basic commands are pipe delimited.
        // This is really for Stage 2.
        raw_cmd = strtok(str, PIPE_DELIM);

        cmd_list = (cmd_list_t *) calloc(1, sizeof(cmd_list_t));

        // This block should probably be put into its own function.
        cmd_count = 0;
        while (raw_cmd != NULL ) {
            cmd_t *cmd = (cmd_t *) calloc(1, sizeof(cmd_t));

            cmd->raw_cmd = strdup(raw_cmd);
            cmd->list_location = cmd_count++;

            if (cmd_list->head == NULL) {
                // An empty list.
                cmd_list->tail = cmd_list->head = cmd;
            }
            else {
                // Make this the last in the list of cmds
                cmd_list->tail->next = cmd;
                cmd_list->tail = cmd;
            }
            cmd_list->count++;

            // Get the next raw command.
            raw_cmd = strtok(NULL, PIPE_DELIM);
        }

        //save stdout and stdin in case they are redirected in parse function
        saved_stdout = dup(STDOUT_FILENO);
        saved_stdin = dup(STDIN_FILENO);

        // Now that I have a linked list of the pipe delimited commands,
        // go through each individual command.
        parse_commands(cmd_list);

        // This is a really good place to call a function to exec the
        // the commands just parsed from the user's command line.
        exec_commands(cmd_list);

        //restore stdout and stdin from any redirected state
        if (dup2(saved_stdout, STDOUT_FILENO) < 0)
        {
            fprintf(stderr, "failed to restore stdout (line %d)\n", __LINE__);
            free_list(cmd_list);
            cmd_list = NULL;
            exit(EXIT_FAILURE);
        }
        close(saved_stdout);
        if (dup2(saved_stdin, STDIN_FILENO) < 0)
        {
            fprintf(stderr, "failed to restore stdin (line %d)\n", __LINE__);
            free_list(cmd_list);
            cmd_list = NULL;
            exit(EXIT_FAILURE);
        }
        close(saved_stdin);


        free_list(cmd_list);
        cmd_list = NULL;
    }

    return(EXIT_SUCCESS);
}

void 
simple_argv(int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "hv")) != -1) {
        switch (opt) {
        case 'h': //help
            fprintf(stdout, "You must be out of your Vulcan mind if you think\n"
                    "I'm going to put helpful things in here.\n\n");
            exit(EXIT_SUCCESS);
            break;
        case 'v': //verbose
            is_verbose++;
            if (is_verbose) {
                fprintf(stderr, "verbose: verbose option selected: %d\n"
                        , is_verbose);
            }
            break;
        case '?':
            fprintf(stderr, "*** Unknown option used, ignoring. ***\n");
            break;
        default:
            fprintf(stderr, "*** Oops, something strange happened <%c> ... ignoring ...***\n", opt);
            break;
        }
    }
}

void 
exec_commands(cmd_list_t *cmds) 
{
    cmd_t *cmd = cmds->head;
    char **argv = {0};

    if (1 == cmds->count) {
        if (!cmd->cmd) return; //empty command, bail
        if (0 == strcmp(cmd->cmd, CD_CMD)) //cd 
        {
            if (0 == cmd->param_count) //cd no argument
            {
                if (chdir(getenv("HOME")) != 0)
                {
                    fprintf(stderr, "cd failed (line %d)\n", __LINE__);
                }
            }
            else //cd with argument
            {
                if (chdir(cmd->param_list->param) != 0)
                {
                    fprintf(stderr, "cd failed on (line %d)\n", __LINE__);
                }
            }
        }
        else if (0 == strcmp(cmd->cmd, CWD_CMD)) //cwd
        {
            char str[MAXPATHLEN];
            getcwd(str, MAXPATHLEN); 
            printf(" " CWD_CMD ": %s\n", str);
        }
        else if (0 == strcmp(cmd->cmd, ECHO_CMD)) //echo
        {
           param_t *current = cmd->param_list;
           while (current)
           {
              fprintf(stdout, "%s", current->param);
              if (current->next) fprintf(stdout, " ");
              current = current->next;
           }

           fprintf(stdout, "\n");
        }
        else if (0 == strcmp(cmd->cmd, HISTORY_CMD)) //display history
        {
           int num_commands = 0;
           for (int i = 0; i < HIST; ++i)
              if (hist[i]) ++num_commands;

           //display every item in history, oldest to newest
           for (int i = 0, j = num_commands; i < num_commands; ++i, --j)
              fprintf(stdout, "   %d  %s\n", i + 1, hist[j - 1]);
        }
        else //external commands
        {
            int status = 0;
            argv = make_ragged(cmd);

            //fork, exec with child
            child_pid = fork();
            if (child_pid == -1) fprintf(stderr, "fork failed (line %d)\n", __LINE__);
            else if (child_pid == 0) //child process
            {
               execvp(argv[0], argv); //execvp only returns on failure
               fprintf(stdout, "%s: command not found\n", argv[0]);
               //cleanup memory then leave function
               free_ragged(argv);
               free_list(cmds);
               for (int i = 0; i < HIST; ++i)
                  if (hist[i]) free(hist[i]);
               exit(EXIT_FAILURE);
            }
            
            else //parent process
            {
               wait(&status);
               //check if child was killed by forwarded SIGINT signal
               if (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT)
                  fprintf(stdout, "child killed\n");
            }

            free_ragged(argv);
        }
    }
    else //multiple commands on command line
    {
        int p_trail = 0;
        int status = 0;

        while (cmd)
        {
           int P[2];
           pid_t mypid = 0;
           argv = make_ragged(cmd);

           //create pipe if not the last command 
           if (cmd->next)
           {
              if (pipe(P) == -1)
              {
                 fprintf(stderr, "pipe creation failed (line %d)\n", __LINE__);
                 //cleanup memory then exit
                 free_ragged(argv);
                 free_list(cmds);
                 for (int i = 0; i < HIST; ++i)
                     if (hist[i]) free(hist[i]);
                 exit(EXIT_FAILURE);
              }
           }

           //fork
           mypid = fork();
           if (mypid == -1) //error
           {
              fprintf(stderr, "fork failed (line %d)\n", __LINE__);
              //cleanup memory then exit
              free_ragged(argv);
              free_list(cmds);
              for (int i = 0; i < HIST; ++i)
                  if (hist[i]) free(hist[i]);
              exit(EXIT_FAILURE);
           }
           else if (mypid == 0) //child
           {
              if (cmd != cmds->head) //not first command
              {
                 dup2(p_trail, STDIN_FILENO);  //p_trail is input side of pipe from previous command in pipeline
              }
              if (cmd->next) //not last command
              {
                 dup2(P[WRITE], STDOUT_FILENO);
                 close(P[READ]);
                 close(P[WRITE]);
              }
              execvp(argv[0], argv); //only returns on failure
              //cleanup memory then exit
              fprintf(stdout, "%s: command not found\n", argv[0]);
              free_ragged(argv);
              free_list(cmds);
              for (int i = 0; i < HIST; ++i)
                  if (hist[i]) free(hist[i]);
              exit(EXIT_FAILURE);
           }
           else //parent
           {
              if (cmd != cmds->head) //not first command
              {
                 close(p_trail);  //p_trail is input side of pipe from previous command in pipeline
              }
              if (cmd->next) //not last command
              {
                 close(P[WRITE]);
                 p_trail = P[READ];
              }
           }
           free_ragged(argv);
           cmd = cmd->next; //next command
        } //end while

        //reap all children
        for (int i = 0; i < cmds->count; ++i)
        {
            wait(&status);
            //check if child was killed by forwarded SIGINT signal
            if (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT)
               fprintf(stdout, "child killed\n");
        }
    }
}

//make a null-terminated ragged array for a single command.
//argv[0] will be the command, followed by all its parameters,
//ending with a null ptr after the last parameter.
//this function allocates dynamic memory
char **
make_ragged(cmd_t *cmd)
{
   char **argv = NULL;
   int i = 1;

   //create "argv" ragged array to pass to execvp
   param_t *current = cmd->param_list;
   argv = malloc(sizeof(char *) * (cmd->param_count + 2));
   argv[0] = strdup(cmd->cmd);
   if (current) //parameters exist, add them to argv after the command
   {
      for (; current; ++i)
      {
         argv[i] = strdup(current->param);
         current = current->next;
      }
   }
   argv[i] = NULL; //null terminate after last item

   return argv;
}

//deallocate dynamic memory for ragged array argv
void
free_ragged(char **argv)
{
   if (!argv) return;
   for (int i = 0; argv[i]; ++i) 
   {
      if (argv[i]) free(argv[i]);
      argv[i] = NULL;
   }
   if (argv) free(argv);
   argv = NULL;
}

//signal handler for SIGINT (ctrl-C).  Forwards SIGINT to child
//process if it exists, ignores SIGINT if there is no child.
void
signal_handler(int signo)
{
   if (signo == SIGINT && child_pid > 0)
   {
      kill(child_pid, SIGINT);
   }
}

void
free_list(cmd_list_t *cmd_list)
{
   cmd_t *current = NULL;
   if (!cmd_list) return;
   current = cmd_list->head;

   while (current)
   {
      cmd_t *temp = current->next;
      free_cmd(current);
      current = temp;
   }
   free(cmd_list);
   cmd_list = NULL;
}

void
print_list(cmd_list_t *cmd_list)
{
    cmd_t *cmd = cmd_list->head;

    while (NULL != cmd) {
        print_cmd(cmd);
        cmd = cmd->next;
    }
}

void
free_cmd (cmd_t *cmd)
{
   param_t *current = NULL;
   if (!cmd) return;
   current = cmd->param_list;

   while (current) //loop to free the param linked list
   {
      param_t *temp = current->next;
      if (current->param) free(current->param);
      current->param = NULL;
      free(current);
      current = temp;
   }
   if (cmd->cmd) free(cmd->cmd);
   if (cmd->raw_cmd) free(cmd->raw_cmd);
   if (cmd->input_file_name) free(cmd->input_file_name);
   if (cmd->output_file_name) free(cmd->output_file_name);
   cmd->cmd = NULL;
   cmd->raw_cmd = NULL;
   cmd->input_file_name = NULL;
   cmd->output_file_name = NULL;
   if (cmd) free(cmd);
   cmd = NULL;
}

// Oooooo, this is nice. Show the fully parsed command line in a nice
// easy to read and digest format.
void
print_cmd(cmd_t *cmd)
{
    param_t *param = NULL;
    int pcount = 1;

    fprintf(stderr,"raw text: +%s+\n", cmd->raw_cmd);
    fprintf(stderr,"\tbase command: +%s+\n", cmd->cmd);
    fprintf(stderr,"\tparam count: %d\n", cmd->param_count);
    param = cmd->param_list;

    while (NULL != param) {
        fprintf(stderr,"\t\tparam %d: %s\n", pcount, param->param);
        param = param->next;
        pcount++;
    }

    fprintf(stderr,"\tinput source: %s\n"
            , (cmd->input_src == REDIRECT_FILE ? "redirect file" :
               (cmd->input_src == REDIRECT_PIPE ? "redirect pipe" : "redirect none")));
    fprintf(stderr,"\toutput dest:  %s\n"
            , (cmd->output_dest == REDIRECT_FILE ? "redirect file" :
               (cmd->output_dest == REDIRECT_PIPE ? "redirect pipe" : "redirect none")));
    fprintf(stderr,"\tinput file name:  %s\n"
            , (NULL == cmd->input_file_name ? "<na>" : cmd->input_file_name));
    fprintf(stderr,"\toutput file name: %s\n"
            , (NULL == cmd->output_file_name ? "<na>" : cmd->output_file_name));
    fprintf(stderr,"\tlocation in list of commands: %d\n", cmd->list_location);
    fprintf(stderr,"\n");
}

// Remember how I told you that use of alloca() is
// dangerous? You can trust me. I'm a professional.
// And, if you mention this in class, I'll deny it
// ever happened. What happens in stralloca stays in
// stralloca.
#define stralloca(_R,_S) {(_R) = alloca(strlen(_S) + 1); strcpy(_R,_S);}

void
parse_commands(cmd_list_t *cmd_list)
{
    cmd_t *cmd = cmd_list->head;
    char *arg;
    char *raw;

    while (cmd) {
        // Because I'm going to be calling strtok() on the string, which does
        // alter the string, I want to make a copy of it. That's why I strdup()
        // it.
        // Given that command lines should not be tooooo long, this might
        // be a reasonable place to try out alloca(), to replace the strdup()
        // used below. It would reduce heap fragmentation.
        //raw = strdup(cmd->raw_cmd);

        // Following my comments and trying out alloca() in here. I feel the rush
        // of excitement from the pending doom of alloca(), from a macro even.
        // It's like double exciting.
        stralloca(raw, cmd->raw_cmd);

        arg = strtok(raw, SPACE_DELIM);
        if (NULL == arg) {
            // The way I've done this is like ya'know way UGLY.
            // Please, look away.
            // If the first command from the command line is empty,
            // ignore it and move to the next command.
            // No need free with alloca memory.
            //free(raw);
            cmd = cmd->next;
            // I guess I could put everything below in an else block.
            continue;
        }
        // I put something in here to strip out the single quotes if
        // they are the first/last characters in arg.
        if (arg[0] == '\'') {
            arg++;
        }
        if (arg[strlen(arg) - 1] == '\'') {
            arg[strlen(arg) - 1] = '\0';
        }
        cmd->cmd = strdup(arg);
        // Initialize these to the default values.
        cmd->input_src = REDIRECT_NONE;
        cmd->output_dest = REDIRECT_NONE;

        while ((arg = strtok(NULL, SPACE_DELIM)) != NULL)
        {
            if (strcmp(arg, REDIR_IN) == 0)
            {
                // redirect stdin

                // If the input_src is something other than REDIRECT_NONE, then
                // this is an improper command.

                // If this is anything other than the FIRST cmd in the list,
                // then this is an error.

                int fd;

                //error check
                if (cmd->input_src != REDIRECT_NONE || cmd_list->head != cmd)
                {
                   fprintf(stderr, "improper redirect of input (line %d)\n", __LINE__);
                   exit(EXIT_FAILURE);
                }

                cmd->input_file_name = strdup(strtok(NULL, SPACE_DELIM));
                cmd->input_src = REDIRECT_FILE;

                fd = open(cmd->input_file_name, O_RDONLY);
                if (fd < 0)
                {
                   fprintf(stderr, "redirect input failed (line %d)\n", __LINE__);
                   exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            else if (strcmp(arg, REDIR_OUT) == 0)
            {
                
                // redirect stdout
                       
                // If the output_dest is something other than REDIRECT_NONE, then
                // this is an improper command.

                // If this is anything other than the LAST cmd in the list,
                // then this is an error.

                int fd;

                //error check
                if (cmd->output_dest != REDIRECT_NONE || cmd->next != NULL)
                {
                   fprintf(stderr, "improper redirect of output (line %d)\n", __LINE__);
                   exit(EXIT_FAILURE);
                }

                cmd->output_file_name = strdup(strtok(NULL, SPACE_DELIM));
                cmd->output_dest = REDIRECT_FILE;

                fd = open(cmd->output_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (fd < 0)
                {
                   fprintf(stderr, "redirect output failed (line %d)\n", __LINE__);
                   exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            else
            {
                // add next param
                param_t *param = (param_t *) calloc(1, sizeof(param_t));
                param_t *cparam = cmd->param_list;

                cmd->param_count++;
                // Put something in here to strip out the single quotes if
                // they are the first/last characters in arg.
                if (arg[0] == '\'') {
                    arg++;
                }
                if (arg[strlen(arg) - 1] == '\'') {
                    arg[strlen(arg) - 1] = '\0';
                }
                param->param = strdup(arg);
                if (NULL == cparam) {
                    cmd->param_list = param;
                }
                else {
                    // I should put a tail pointer on this.
                    while (cparam->next != NULL) {
                        cparam = cparam->next;
                    }
                    cparam->next = param;
                }
            }
        }
        // This could overwite some bogus file redirection.
        if (cmd->list_location > 0) {
            cmd->input_src = REDIRECT_PIPE;
        }
        if (cmd->list_location < (cmd_list->count - 1)) {
            cmd->output_dest = REDIRECT_PIPE;
        }

        // No need free with alloca memory.
        //free(raw);
        cmd = cmd->next;
    }

    if (is_verbose > 0) {
        print_list(cmd_list);
    }
}
