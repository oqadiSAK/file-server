#include "include/types.h"
#include "include/command_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <termios.h>

void check_usage(int argc, char *argv[]);
int check_connection_res(connection_response_t *response);
int open_client_fifo(char *client_fifo_name, int mode);
int parse_server_pid(char *str);
int parse_connection_type(char *str);
void create_client_fifo(char *client_fifo_name);
int connect_server_fifo(char *server_fifo_name);
sem_t *create_client_connection_sem();
void set_signal_handlers();
void bibo_client(int client_pid, int server_fd, connection_type_t connection_type, sem_t *client_connection_sem,
                 connection_response_t *response, char *client_fifo_name_read, char *client_fifo_name_write);

void send_connection_req(int client_pid, int server_fd, connection_type_t connection_type);
connection_response_t *create_res_shm();
void disable_terminal();
void enable_terminal();

struct termios orig_termios;
volatile sig_atomic_t signal_received = 0;
struct sigaction sa_clean;

void cleaner_signal_handler()
{
    signal_received = 1;
}

int main(int argc, char *argv[])
{
    int server_fd;
    char server_fifo_name[SERVER_FIFO_NAME_LEN], client_fifo_name_write[CLIENT_WRITE_FIFO_NAME_LEN], client_fifo_name_read[CLIENT_READ_FIFO_NAME_LEN];
    pid_t server_pid, client_pid;
    connection_type_t connection_type;
    sem_t *client_connection_sem;
    connection_response_t *response;

    check_usage(argc, argv);
    server_pid = parse_server_pid(argv[2]);
    connection_type = parse_connection_type(argv[1]);
    client_pid = getpid();
    snprintf(client_fifo_name_write, CLIENT_WRITE_FIFO_NAME_LEN, CLIENT_WRITE_FIFO_TEMPLATE, (long)client_pid);
    snprintf(client_fifo_name_read, CLIENT_READ_FIFO_NAME_LEN, CLIENT_READ_FIFO_TEMPLATE, (long)client_pid);
    snprintf(server_fifo_name, SERVER_FIFO_NAME_LEN, SERVER_FIFO_TEMPLATE, (long)server_pid);
    create_client_fifo(client_fifo_name_write);
    create_client_fifo(client_fifo_name_read);
    client_connection_sem = create_client_connection_sem();
    response = create_res_shm();
    server_fd = connect_server_fifo(server_fifo_name);
    set_signal_handlers();
    bibo_client(client_pid, server_fd, connection_type, client_connection_sem, response,
                client_fifo_name_read, client_fifo_name_write);
    return 0;
}

void set_signal_handlers()
{
    sa_clean.sa_handler = cleaner_signal_handler;
    sigaction(SIGINT, &sa_clean, NULL);
    sigaction(SIGTSTP, &sa_clean, NULL);
    sigaction(SIGQUIT, &sa_clean, NULL);
}

void bibo_client(int client_pid, int server_fd, connection_type_t connection_type, sem_t *client_connection_sem,
                 connection_response_t *response, char *client_fifo_name_read, char *client_fifo_name_write)
{
    int client_fd_write, client_fd_read, flag;
    send_connection_req(client_pid, server_fd, connection_type);
    sem_wait(client_connection_sem);
    flag = check_connection_res(response);
    disable_terminal();
    sem_wait(client_connection_sem);
    enable_terminal();
    if ((client_fd_read = open(client_fifo_name_read, O_RDONLY)) == -1)
    {
        perror("Error while opening read fifo");
        exit(EXIT_FAILURE);
    }
    if ((client_fd_write = open(client_fifo_name_write, O_WRONLY)) == -1)
    {
        perror("Error whlie opening write fifo");
        exit(EXIT_FAILURE);
    }
    if (flag == 1)
    {
        printf(">> Connection established:\n");
        fflush(stdout);
    }
    while (1)
    {
        printf("\n> ");
        fflush(stdout);

        char command_str[MAX_COMMAND_LENGTH];
        command_t command;
        if (fgets(command_str, sizeof(command_str), stdin) == NULL)
        {
            if (errno == EINTR)
            {
                command.type = QUIT;
                command.line = -1;
                memset(command.file, 0, sizeof(command.file));
                memset(command.string, 0, sizeof(command.string));
            }
            else
            {
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            command_str[strcspn(command_str, "\n")] = '\0';
            if (parse_command(command_str, &command) == -1)
            {
                printf("Invalid command\n");
                fflush(stdout);
                continue;
            }
        }
        if (write(client_fd_write, &command, sizeof(command)) == -1)
        {
            if (errno == EINTR)
            {
                printf("\nServer is closed\n");
                close(server_fd);
                close(client_fd_read);
                close(client_fd_write);
                exit(EXIT_SUCCESS);
            }

            perror("Error writing request");
            continue;
        }
        if (command.type == QUIT)
        {
            printf("\nSending write request to server log file\n");
            printf("waiting for logfile...\n");
        }
        else if (command.type == KILLSERVER)
        {
            printf("\nServer is closed\n");
            close(server_fd);
            close(client_fd_read);
            close(client_fd_write);
            exit(EXIT_SUCCESS);
        }

        else if (command.type == DOWNLOAD)
        {
            // Prepare the file for writing
            char file_path[MAX_FILENAME_LENGTH];
            int is_file_exist;
            sprintf(file_path, "%s", command.file);
            read(client_fd_read, &is_file_exist, sizeof(is_file_exist));
            if (!is_file_exist)
            {
                printf("There is no such a file to download\n");
                continue;
            }

            int file_fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
            if (file_fd == -1)
            {
                perror("Error opening file for writing");
                continue;
            }

            response_t response;
            long int total_read = 0;
            response.is_complete = 0;

            // Receive and write the response to the file in chunks
            while (!response.is_complete)
            {
                sem_wait(client_connection_sem);
                ssize_t bytes_read = read(client_fd_read, &response, sizeof(response));
                if (bytes_read == -1)
                {
                    if (errno == EINTR)
                    {
                        printf("\nServer is closed\n");
                        close(server_fd);
                        close(client_fd_read);
                        close(client_fd_write);
                        exit(EXIT_SUCCESS);
                    }

                    perror("Error reading response");
                    continue;
                }
                printf("%d bytes downloaded..\n", strlen(response.content));
                total_read += strlen(response.content);
                ssize_t bytes_written = write(file_fd, response.content, strlen(response.content));
                if (bytes_written == -1)
                {
                    perror("Error writing to file");
                    break;
                }

                // Reset the buffer
                memset(response.content, 0, sizeof(response.content));
            }

            // Close the file
            close(file_fd);
            printf("File downloaded successfully. (%ld bytes)\n", total_read);

            fflush(stdout);
            continue;
        }
        else if (command.type == UPLOAD)
        {
            // Open the file for reading
            int is_file_exist = 0;
            int file_fd = open(command.file, O_RDONLY);
            if (file_fd == -1)
            {
                printf("No file to upload");
                fflush(stdout);
                continue;
            }
            char file_sem_name[FILE_SEM_NAME_LEN];
            snprintf(file_sem_name, FILE_SEM_NAME_LEN, FILE_SEM_NAME_TEMPLATE, command.file);
            sem_t *sem_file = sem_open(file_sem_name, O_CREAT, 0777, 1);
            if (sem_file == SEM_FAILED)
            {
                perror("sem_open");
                exit(EXIT_FAILURE);
            }
            sem_wait(sem_file);
            read(client_fd_read, &is_file_exist, sizeof(is_file_exist));
            if (is_file_exist == 1)
            {
                printf("\nFile already exist!\n");
                continue;
            }
            // Read and send the file contents in chunks
            response_t response;
            ssize_t bytes_read;
            long int total_written = 0;
            while ((bytes_read = read(file_fd, response.content, sizeof(response.content))) > 0)
            {
                // Set the response properties
                response.is_complete = 0;
                response.is_exit = 0;

                // Send the response to the server
                if (write(client_fd_write, &response, sizeof(response)) == -1)
                {
                    perror("Error writing to server");
                    break;
                }
                sem_post(client_connection_sem);
                printf("%d bytes uploaded..\n", strlen(response.content));
                total_written += strlen(response.content);
                // Reset the buffer
                memset(response.content, 0, sizeof(response.content));
            }

            // Close the file descriptor
            close(file_fd);
            sem_post(sem_file);
            sem_close(sem_file);
            sem_unlink(file_sem_name);
            // Send the exit flag to indicate the upload is complete
            response.is_complete = 1;
            response.is_exit = 1;
            if (write(client_fd_write, &response, sizeof(response)) == -1)
            {
                perror("Error writing to server");
                break;
            }
            printf("File uploaded successfully. (%ld bytes)\n", total_written);
            response.is_exit = 0;
            continue;
        }

        response_t response;
        response.is_complete = 0;

        while (!response.is_complete)
        {
            sem_wait(client_connection_sem);
            // Read the response from the server in chunks of 2048 bytes
            if (read(client_fd_read, &response, sizeof(response)) == -1)
            {
                if (errno == EINTR)
                {
                    printf("\nServer is closed\n");
                    close(server_fd);
                    close(client_fd_read);
                    close(client_fd_write);
                    exit(EXIT_SUCCESS);
                }
                perror("read");
                exit(EXIT_FAILURE);
            }

            if (response.is_exit)
            {
                printf("logfile write request granted\n");
                printf("bye..\n");
                close(client_fd_read);
                close(client_fd_write);
                exit(EXIT_SUCCESS);
            }

            // Process the chunk of data received from the server
            printf("%s", response.content);
            fflush(stdout);

            // Reset the buffer
            memset(response.content, 0, sizeof(response.content));
        }
    }

    // Clean up
    unlink(client_fifo_name_read);
    unlink(client_fifo_name_write);
}

void check_usage(int argc, char *argv[])
{
    if (argc != 3 || (strcmp(argv[1], "connect") != 0 && strcmp(argv[1], "tryConnect") != 0))
    {
        fprintf(stderr, "Usage: %s <connect/tryConnect> ServerPID\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

int check_connection_res(connection_response_t *response)
{
    int flag = 0;
    if (*response == WAITING)
    {
        printf(">> Waiting for Que.. \n");
        fflush(stdout);
        flag = 1;
    }
    else if (*response == CONNECTED)
    {
        printf(">> Connection established:\n");
        fflush(stdout);
    }
    else if (*response == LEAVE)
    {
        printf(">> Que full, leaving...\n");
        fflush(stdout);
        exit(EXIT_SUCCESS);
    }
    return flag;
}

int open_client_fifo(char *client_fifo_name, int mode)
{
    int client_fd = open(client_fifo_name, mode);
    if (client_fd == -1)
    {
        perror("Error while opening client FIFO");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    return client_fd;
}

int parse_server_pid(char *str)
{
    int server_pid = atoi(str);
    if (server_pid == 0)
    {
        fprintf(stderr, "Invalid ServerPID\n");
        exit(EXIT_FAILURE);
    }
    return server_pid;
}

int parse_connection_type(char *str)
{
    if (strcmp(str, "connect") == 0)
        return CONNECT;
    else
        return TRY_CONNECT;
}

void create_client_fifo(char *client_fifo_name)
{
    if (mkfifo(client_fifo_name, 0777) == -1 && errno != EEXIST)
    {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }
}

int connect_server_fifo(char *server_fifo_name)
{
    // Connect to server FIFO
    int server_fd = open(server_fifo_name, O_WRONLY);
    if (server_fd == -1)
    {
        perror("Error while opening server FIFO");
        exit(EXIT_FAILURE);
    }
    return server_fd;
}

sem_t *create_client_connection_sem()
{
    char client_connection_sem_name[CLIENT_SEM_NAME_LEN];
    sem_t *client_connection_sem;

    snprintf(client_connection_sem_name, CLIENT_SEM_NAME_LEN, CLIENT_SEM_NAME_TEMPLATE, (long)getpid());
    client_connection_sem = sem_open(client_connection_sem_name, O_CREAT | O_EXCL, 0777, 0);
    if (client_connection_sem == SEM_FAILED)
    {
        perror("Error while opening client_connection_sem semaphore");
        exit(EXIT_FAILURE);
    }
    return client_connection_sem;
}

void send_connection_req(int client_pid, int server_fd, connection_type_t connection_type)
{
    connection_request_t request = {client_pid, connection_type};
    int bytes_written;
    while ((bytes_written = write(server_fd, &request, sizeof(request))) == -1)
    {
        if (errno != EINTR)
        {
            perror("Error while writing to server FIFO");
            exit(EXIT_FAILURE);
        }
    }
    if (bytes_written < 0)
    {
        perror("Error while writing to server FIFO");
    }
}

connection_response_t *create_res_shm()
{
    connection_response_t *shm_ptr;
    int shm_fd;
    char res_shm_name[RESPOND_SHM_LEN];
    snprintf(res_shm_name, RESPOND_SHM_LEN, RESPOND_SHM_TEMPLATE, (long)getpid());
    shm_fd = shm_open(res_shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Error creating shared memory");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(connection_response_t)) == -1)
    {
        perror("Error resizing shared memory");
        exit(EXIT_FAILURE);
    }
    shm_ptr = (connection_response_t *)mmap(NULL, sizeof(connection_response_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        perror("Error mapping shared memory");
        exit(EXIT_FAILURE);
    }
    return shm_ptr;
}

void disable_terminal()
{
    struct termios new_termios;
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == -1)
    {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    if (tcflush(STDIN_FILENO, TCIFLUSH) == -1)
    {
        perror("tcflush");
        exit(EXIT_FAILURE);
    }
}

void enable_terminal()
{
    if (tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios) == -1)
    {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}
