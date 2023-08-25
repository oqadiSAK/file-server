#ifndef TYPES_H
#define TYPES_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_FILENAME_LENGTH 256
#define CHUNK_SIZE 2048
#define MAX_PATH_LENGTH 4096
#define MAX_WRITE_STRING_LENGTH 256
#define MAX_COMMAND_TYPE_LENGTH 20
#define MAX_COMMAND_LENGTH 512
#define SERVER_FIFO_TEMPLATE "/tmp/bibo_server.%ld"
#define SERVER_FIFO_NAME_LEN (sizeof(SERVER_FIFO_TEMPLATE) + 20)
#define CLIENT_WRITE_FIFO_TEMPLATE "/tmp/bibo_client_write.%ld"
#define CLIENT_WRITE_FIFO_NAME_LEN (sizeof(CLIENT_WRITE_FIFO_TEMPLATE) + 20)
#define CLIENT_READ_FIFO_TEMPLATE "/tmp/bibo_client_read.%ld"
#define CLIENT_READ_FIFO_NAME_LEN (sizeof(CLIENT_READ_FIFO_TEMPLATE) + 20)
#define SERVER_LOG_FILE_NAME "server_log"
#define FREE_SLOT_SEM_NAME_TEMPLATE "free_slot_sem.%ld"
#define FREE_SLOT_SEM_NAME_LEN (sizeof(FREE_SLOT_SEM_NAME_TEMPLATE) + 20)
#define CLIENT_SEM_NAME_TEMPLATE "client_sem.%ld"
#define CLIENT_SEM_NAME_LEN (sizeof(FREE_SLOT_SEM_NAME_TEMPLATE) + 20)
#define FILE_SEM_NAME_TEMPLATE "client_sem.%s"
#define FILE_SEM_NAME_LEN (sizeof(FILE_SEM_NAME_TEMPLATE) + MAX_FILENAME_LENGTH)
#define RESPOND_SHM_TEMPLATE "res_shm.%ld"
#define RESPOND_SHM_LEN (sizeof(FREE_SLOT_SEM_NAME_TEMPLATE) + 20)
#define SHM_QUEUE_NAME_TEMPLATE "/que.%ld"
#define SHM_QUEUE_NAME_LEN (sizeof(SHM_QUEUE_NAME_TEMPLATE) + 20)

typedef enum
{
    HELP,
    LIST,
    READF,
    WRITET,
    UPLOAD,
    DOWNLOAD,
    QUIT,
    KILLSERVER,
    UNKNOWN
} command_type_t;

typedef enum
{
    CONNECT,
    TRY_CONNECT
} connection_type_t;

typedef struct
{
    command_type_t type;
    command_type_t sub_type;
    char file[MAX_FILENAME_LENGTH];       // filename
    int line;                             // line number
    char string[MAX_WRITE_STRING_LENGTH]; // string to write (for WRITET command)
} command_t;

typedef struct
{
    pid_t pid;
    connection_type_t connection_type;

} connection_request_t;

typedef enum
{
    WAITING,
    CONNECTED,
    LEAVE
} connection_response_t;

typedef struct
{
    pid_t pid;
    pid_t counter_id;
    connection_type_t connection_type;
    char fifo_name_write[CLIENT_WRITE_FIFO_NAME_LEN];
    char fifo_name_read[CLIENT_READ_FIFO_NAME_LEN];

} client_info_t;

typedef struct
{
    char content[CHUNK_SIZE];
    int is_complete;
    int is_exit;
} response_t;
#endif