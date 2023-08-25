#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "logger.h"

/*
 Function to parse a command from a string and fill a command_t struct.
 Returns 0 on success, or -1 on failure.
*/
int parse_command(char *input_str, command_t *command);
command_type_t get_type(char *str);
char *get_message(command_type_t type);
void init_command(command_t *command);
/*
 For testing purposes
*/
void log_command(command_t *command, int log_fd);
void log_command_file(command_t *command, int log_fd);
void log_command_str(command_t *command, int log_fd);

#endif