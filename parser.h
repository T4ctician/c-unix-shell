#ifndef _PARSER_H
#define _PARSER_H

/*
 * Parser.h
 * Data structures and various defines for parser.c
 * Author : Michael Roberts <mroberts@it.net.au>
 * Last Update : 15/07/01
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*The length of the command line.*/
#define CMD_LENGTH 256

/*Whitespaces that are searched for*/
// nick modified this
//static const char white_space[2] = { (char) 0x20, (char) 0x09 };
static const char white_space[3] = { (char) 0x20, (char) 0x09, (char) 0x00 };


/*The Structure we create for the commands.*/
typedef struct Command_struct
{
   char *com_name;
   char **argv;
   int background;
   char *redirect_in;
   char *redirect_out;
   char *redirect_err;
   int pipe_to;
}
command;


/* Function prototypes added by Nick Nelissen 11/9/2001 */
command ** process_cmd_line(char *cmd,int);
void process_cmd(char *cmd, command * result);
void process_simple_cmd(char *cmd, command * result);
void clean_up(command ** cmd);
void clean_up(command ** cmd);
void clean_up(command ** cmd);
void trim_whitespace(char *str);

#endif
