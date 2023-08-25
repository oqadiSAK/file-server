#include "../include/command_parser.h"

int parse_command(char *input_str, command_t *command)
{
    char cmd_type_str[MAX_COMMAND_TYPE_LENGTH];
    char cmd_sub_type_str[MAX_COMMAND_TYPE_LENGTH];
    char file[MAX_FILENAME_LENGTH];
    int line;
    char string[MAX_WRITE_STRING_LENGTH];

    init_command(command);
    if (sscanf(input_str, "%s", cmd_type_str) != 1)
    {
        return -1;
    }
    if (strcmp(cmd_type_str, "help") == 0)
    {
        command->type = HELP;
        if (sscanf(input_str, "%s %s", cmd_type_str, cmd_sub_type_str) == 2)
        {
            command->sub_type = get_type(cmd_sub_type_str);
            command->line = line;
            return 0;
        }
        if (command->sub_type == UNKNOWN)
            return -1;
        else
            return 0;
    }
    else if (strcmp(cmd_type_str, "list") == 0)
    {
        command->type = LIST;
        return 0;
    }
    else if (strcmp(cmd_type_str, "readF") == 0)
    {
        command->type = READF;
        if (sscanf(input_str, "%s %s %d", cmd_type_str, file, &line) == 3)
        {
            strcpy(command->file, file);
            command->line = line;
            return 0;
        }
        else if (sscanf(input_str, "%s %s", cmd_type_str, file) == 2)
        {
            strcpy(command->file, file);
            command->line = -1;
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else if (strcmp(cmd_type_str, "writeT") == 0)
    {
        command->type = WRITET;
        if (sscanf(input_str, "%s %s %d %[^\n]", cmd_type_str, file, &line, string) == 4)
        {
            strcpy(command->file, file);
            command->line = line;
            strcpy(command->string, string);
            return 0;
        }
        else if (sscanf(input_str, "%s %s %[^\n]", cmd_type_str, file, string) == 3)
        {
            strcpy(command->file, file);
            command->line = -1;
            strcpy(command->string, string);
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else if (strcmp(cmd_type_str, "upload") == 0)
    {
        command->type = UPLOAD;
        if (sscanf(input_str, "%s %s", cmd_type_str, file) == 2)
        {
            strcpy(command->file, file);
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else if (strcmp(cmd_type_str, "download") == 0)
    {
        command->type = DOWNLOAD;
        if (sscanf(input_str, "%s %s", cmd_type_str, file) == 2)
        {
            strcpy(command->file, file);
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else if (strcmp(cmd_type_str, "quit") == 0)
    {
        command->type = QUIT;
        return 0;
    }
    else if (strcmp(cmd_type_str, "killServer") == 0)
    {
        command->type = KILLSERVER;
        return 0;
    }
    else
    {
        command->type = UNKNOWN;
        return -1;
    }
}

command_type_t get_type(char *str)
{
    if (strcmp(str, "list") == 0)
        return LIST;
    else if (strcmp(str, "readF") == 0)
        return READF;
    else if (strcmp(str, "writeT") == 0)
        return WRITET;
    else if (strcmp(str, "upload") == 0)
        return UPLOAD;
    else if (strcmp(str, "download") == 0)
        return DOWNLOAD;
    else if (strcmp(str, "quit") == 0)
        return QUIT;
    else if (strcmp(str, "killServer") == 0)
        return KILLSERVER;
    else
        return UNKNOWN;
}

char *get_message(command_type_t type)
{
    if (type == HELP)
        return "Possible client requests:\nhelp, list, readF, writeT, upload, download, quit, killServer\n";
    else if (type == LIST)
        return "sends a request to display the list of files in Servers directory\n";
    else if (type == READF)
        return "readF <file> <line #>\nrequests to display the # line of the <file>, if no line number is given the whole contents of the file is requested\n";
    else if (type == WRITET)
        return "writeT <file> <line #> <string>\nrequest to write the content of “string” to the #th line the <file>, if the line # is not givenwrites to the end of file. If the file does not exists in Servers directory creates and edits thefile at the same time\n";
    else if (type == UPLOAD)
        return "upload <file>\nuploads the file from the current working directory of client to the Servers directory\n";
    else if (type == DOWNLOAD)
        return "download <file>\nrequest to receive <file> from Servers directory to client side\n";
    else if (type == QUIT)
        return "Send write request to Server side log file and quits\n";
    else if (type == KILLSERVER)
        return "Sends a kill request to the Server\n";
    else
        return "Invalid command\n";
}

void init_command(command_t *command)
{
    memset(command->file, 0, sizeof(command->file));
    command->line = -1;
    memset(command->string, 0, sizeof(command->string));
}

// for testing purposes
void log_command(command_t *command, int log_fd)
{
    my_log(log_fd, "Type: ");
    switch (command->type)
    {
    case HELP:
        my_log(log_fd, "HELP\n");
        break;
    case LIST:
        my_log(log_fd, "LIST\n");
        break;
    case READF:
        my_log(log_fd, "READF\n");
        break;
    case WRITET:
        my_log(log_fd, "WRITET\n");
        break;
    case UPLOAD:
        my_log(log_fd, "UPLOAD\n");
        break;
    case DOWNLOAD:
        my_log(log_fd, "DOWNLOAD\n");
        break;
    case QUIT:
        my_log(log_fd, "QUIT\n");
        break;
    case KILLSERVER:
        my_log(log_fd, "KILLSERVER\n");
        break;
    default:
        my_log(log_fd, "UNKNOWN\n");
        break;
    }

    log_command_file(command, log_fd);
    my_log(log_fd, "Line: %d\n", command->line);
    log_command_str(command, log_fd);
}

void log_command_file(command_t *command, int log_fd)
{
    my_log(log_fd, "File: ");
    if (strlen(command->file) == 0)
        my_log(log_fd, "N/A\n");
    else
        my_log(log_fd, "%s\n", command->file);
}

void log_command_str(command_t *command, int log_fd)
{
    my_log(log_fd, "String: ");
    if (strlen(command->string) == 0)
        my_log(log_fd, "N/A\n");
    else
        my_log(log_fd, "%s\n", command->string);
}