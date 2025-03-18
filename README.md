# c-unix-shell
Custom UNIX Shell in C

This project is a custom shell implementation in C, designed to mimic the behavior of the Bash shell (excluding the prompt style). The shell can handle:

- At least 100 commands

- At least 1000 arguments per command
 
It remains robust under signals such as CTRL-C, CTRL- \ , and CTRL-Z, ensuring the shell does not terminate unexpectedly. The implementation avoids calling or relying on other existing shells to remain fully independent.

Built-in Commands
- prompt: Displays a customizable shell prompt.
- pwd: Prints the current working directory.
- cd: Changes the current directory, similar to Bash.
- history: Displays and manages command history.
- exit: Exits the shell program.

Directory Navigation
- Supports directory walking using relative and absolute paths, similar to Bash’s cd.

Wildcard File Expansion
- Handles wildcard characters (*, ?) to expand file paths automatically.

Input/Output/Error Redirection
- Supports standard redirection operators (<, >, 2>) to handle file input and output streams.

Pipelining
- Allows chaining commands with | so the output of one command becomes the input to another.

Background Job Execution
- Executes commands in the background by appending &.

Sequential Job Execution
- Runs commands one after another using ;.

Command History and Shortcuts
- Tracks previously executed commands.
- Provides Up/Down Arrow keys navigation.
- Allows quick re-execution of commands via ! (e.g., !3 to run the 3rd command in history).

Environment Inheritance
- Properly inherits environment variables from the parent process.
