#include "include/types.h"
#include "include/queue.h"
#include "include/command_parser.h"
#include "include/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <semaphore.h>
#include <sys/mman.h>

void bibo_server(char *dirname, int max_clients);
queue_t *init_queue(int server_pid);
void init_client_array(client_info_t *clients, int max_client);
void enter_directory(const char *dirname);
void set_signal_handlers();
int set_server_fifo();
sem_t *init_free_slot_sem();
int *init_counter();
client_info_t read_request(int server_fd, int log_fd);
int handle_client(client_info_t *current_client, int *counter, sem_t *client_connection_sem, char *dirname, int log_fd, int max_clients);
int open_client_fifo(char *client_fifo_name, int mode);
sem_t *connect_client_connection_sem(int client_pid);
connection_response_t *connect_client_shm(int client_pid);
void add_mask();
void remove_mask();
void clean_up(int client_fifo_fd_read, int client_fifo_fd_write, sem_t *client_connection_sem);

pid_t *child_pids;
int num_children = 0;
volatile sig_atomic_t signal_received = 0;
struct sigaction sa_clean;
sigset_t mask, orig_mask;
int ppid;
sem_t *free_slot_sem;
queue_t *queue;
int queue_sh_fd;
int *counter;
int counter_sh_fd;

void cleaner_signal_handler()
{
    signal_received = 1;
    int i;
    for (i = 0; i < num_children; i++)
    {
        if (child_pids[i] != -1)
            kill(child_pids[i], SIGINT);
    }
    sem_post(free_slot_sem);
}

int main(int argc, char *argv[])
{
    // Check the command line arguments
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <dirname> <max. #ofClients>\n", argv[0]);
        exit(1);
    }

    // Parse the max number of clients from the command line
    int max_clients = atoi(argv[2]);

    set_signal_handlers();
    bibo_server(argv[1], max_clients);

    return 0;
}

// Main server function
void bibo_server(char *dirname, int max_clients)
{
    int server_fd, log_fd, client_fifo_fd, max_child;
    max_child = max_clients;
    child_pids = malloc(max_clients * sizeof(pid_t));
    memset(child_pids, -1, max_clients * sizeof(pid_t));
    enter_directory(dirname);
    log_fd = create_log_file(dirname);
    ppid = getpid();
    my_log(log_fd, ">> Server started PID %d...\n", ppid);
    my_log(log_fd, ">> Waiting for clients...\n");
    server_fd = set_server_fifo();
    free_slot_sem = init_free_slot_sem();
    counter = init_counter();
    queue = init_queue(ppid);
    *counter = 0;

    while (1)
    {
        // Read request from server fifo
        client_info_t client_info = read_request(server_fd, log_fd);
        queue_enqueue(queue, &client_info);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("Error while fork");
            continue;
        }
        else if (pid == 0)
        {
            add_mask();
            client_info_t *current_client = queue_peek(queue);
            sem_t *client_connection_sem = connect_client_connection_sem(current_client->pid);
            connection_response_t *client_shm = connect_client_shm(current_client->pid);
            fflush(stdout);
            if (queue_size(queue) > max_clients)
            {
                if (current_client->connection_type == TRY_CONNECT)
                {
                    my_log(log_fd, ">> tryConnect request PID %ld... Que FULL... Leaves...\n", (long)current_client->pid);
                    *client_shm = LEAVE;
                    sem_post(client_connection_sem);
                    exit(EXIT_SUCCESS);
                }
                else
                {
                    remove_mask();
                    my_log(log_fd, ">> connect request PID %ld... Que FULL\n", (long)current_client->pid);
                    *client_shm = WAITING;
                    sem_post(client_connection_sem);
                    sem_wait(free_slot_sem);
                    if (signal_received)
                    {
                        kill(current_client->pid, SIGINT);
                        exit(EXIT_SUCCESS);
                    }
                }
            }
            else
            {
                *client_shm = CONNECTED;
                sem_post(client_connection_sem);
            }

            *counter += 1;
            current_client->counter_id = *counter;
            client_fifo_fd = handle_client(current_client, counter, client_connection_sem, dirname, log_fd, max_clients);
            queue_dequeue(queue);
            if (queue_size(queue) >= max_clients)
                sem_post(free_slot_sem);
            close(client_fifo_fd);
            unlink(current_client->fifo_name_write);
            unlink(current_client->fifo_name_read);
            exit(EXIT_SUCCESS);
        }
        else
        {
            if (num_children >= max_child)
            {
                // Resize the child_pids array
                int new_max_children = max_child * 2; // New desired size
                pid_t *resized_child_pids = realloc(child_pids, new_max_children * sizeof(pid_t));

                // Update child_pids pointer to the resized array
                child_pids = resized_child_pids;

                // Update the MAX_CHILDREN value
                max_child = new_max_children;
            }
            child_pids[num_children++] = pid;
        }
    }
}

queue_t *init_queue(int server_pid)
{
    char shm_que_name[SHM_QUEUE_NAME_LEN];
    snprintf(shm_que_name, SHM_QUEUE_NAME_LEN, SHM_QUEUE_NAME_TEMPLATE, (long)server_pid);
    int shm_fd = shm_open(shm_que_name, O_CREAT | O_RDWR, 0777);
    if (shm_fd == -1)
    {
        perror("Error creating shared memory");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(shm_fd, sizeof(queue_t)) == -1)
    {
        perror("Error resizing shared memory");
        exit(EXIT_FAILURE);
    }
    queue_sh_fd = shm_fd;
    queue_t *queue_ptr = (queue_t *)mmap(NULL, sizeof(queue_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (queue_ptr == MAP_FAILED)
    {
        perror("Error mapping shared memory");
        exit(EXIT_FAILURE);
    }

    queue_t *queue = queue_create();
    *queue_ptr = *queue;
    queue_destroy(queue);

    return queue_ptr;
}

void enter_directory(const char *dirname)
{
    // Create the specified directory if it does not already exist
    struct stat st = {0};
    if (stat(dirname, &st) == -1)
    {
        mkdir(dirname, 0777);
    }
}

void set_signal_handlers()
{
    sa_clean.sa_handler = cleaner_signal_handler;
    sigaction(SIGINT, &sa_clean, NULL);
    sigaction(SIGTSTP, &sa_clean, NULL);
    sigaction(SIGQUIT, &sa_clean, NULL);
}

int set_server_fifo()
{
    char serverFifo[SERVER_FIFO_NAME_LEN];
    int dummy_fd, server_fd;
    snprintf(serverFifo, SERVER_FIFO_NAME_LEN, SERVER_FIFO_TEMPLATE, (long)getpid());
    if (mkfifo(serverFifo, 0777) == -1 && errno != EEXIST)
        perror("mkfifo");

    if ((server_fd = open(serverFifo, O_RDONLY)) == -1)
    {
        perror("Error while openning fifo");
        exit(1);
    }

    if ((dummy_fd = open(serverFifo, O_WRONLY)) == -1)
    {
        perror("Error while openning dummy fifo");
    }

    return server_fd;
}

sem_t *init_free_slot_sem()
{
    char free_slot_sem_name[FREE_SLOT_SEM_NAME_LEN];
    sem_t *free_slot_sem;
    snprintf(free_slot_sem_name, FREE_SLOT_SEM_NAME_LEN, FREE_SLOT_SEM_NAME_TEMPLATE, (long)getpid());
    free_slot_sem = sem_open(free_slot_sem_name, O_CREAT | O_EXCL, 0777, 0);
    // Check if semaphore was created successfully
    if (free_slot_sem == SEM_FAILED)
    {
        perror("Failed to create free_slot_sem");
        exit(EXIT_FAILURE);
    }
    return free_slot_sem;
}

int *init_counter()
{
    int *shm_ptr, shm_fd;
    // Create shared memory region
    shm_fd = shm_open("/COUNTER", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Error creating shared memory");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(int)) == -1)
    {
        perror("Error resizing shared memory");
        exit(EXIT_FAILURE);
    }
    counter_sh_fd = shm_fd;
    shm_ptr = (int *)mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        perror("Error mapping shared memory");
        exit(EXIT_FAILURE);
    }
    return shm_ptr;
}

client_info_t read_request(int server_fd, int log_fd)
{
    client_info_t client_info;
    connection_request_t request;
    fflush(stdout);
    ssize_t bytes_read = read(server_fd, &request, sizeof(connection_request_t));
    if (bytes_read == -1 && errno != EINTR)
    {
        perror("Error reading connection request");
        exit(EXIT_FAILURE);
    }
    else if (errno == EINTR)
    {
        int i;
        for (i = 0; i < num_children; i++)
        {
            int status;
            pid_t terminated_pid = waitpid(-1, &status, 0);

            if (terminated_pid == -1)
            {
                perror("Error waiting for child process");
                exit(EXIT_FAILURE);
            }

            printf("Child process with PID %d terminated\n", terminated_pid);
        }
        printf("Parent process is terminating...\n");
        close(log_fd);
        close(server_fd);
        exit(EXIT_SUCCESS);
    }
    else if (bytes_read != sizeof(connection_request_t))
    {
        fprintf(stderr, "Incomplete connection request\n");
        exit(EXIT_FAILURE);
    }
    snprintf(client_info.fifo_name_write, CLIENT_WRITE_FIFO_NAME_LEN, CLIENT_WRITE_FIFO_TEMPLATE, (long)request.pid);
    snprintf(client_info.fifo_name_read, CLIENT_READ_FIFO_NAME_LEN, CLIENT_READ_FIFO_TEMPLATE, (long)request.pid);
    client_info.pid = request.pid;
    client_info.connection_type = request.connection_type;
    client_info.counter_id = -1;
    return client_info;
}

int handle_client(client_info_t *current_client, int *counter, sem_t *client_connection_sem, char *dirname, int log_fd, int max_clients)
{
    int client_fifo_fd_read, client_fifo_fd_write;
    sem_post(client_connection_sem);
    if ((client_fifo_fd_write = open(current_client->fifo_name_read, O_WRONLY)) == -1)
    {
        perror("Error whlie opening write fifo");
        exit(EXIT_FAILURE);
    }
    if ((client_fifo_fd_read = open(current_client->fifo_name_write, O_RDONLY)) == -1)
    {
        perror("Error while opening read fifo");
        exit(EXIT_FAILURE);
    }
    my_log(log_fd, "Client PID %ld connected as “client_%d”\n", current_client->pid, *counter);
    fflush(stdout);
    remove_mask();

    while (1)
    {
        command_t command;
        int bytes_read = read(client_fifo_fd_read, &command, sizeof(command));

        if (bytes_read == -1 && errno != EINTR)
        {
            perror("Error while reading bytes from client fifo read");
        }
        else if (errno == EINTR)
        {
            clean_up(client_fifo_fd_read, client_fifo_fd_write, client_connection_sem);
            kill(current_client->pid, SIGINT);
            my_log(log_fd, "\nClient_%ld disconnected..\n", current_client->counter_id);
            exit(EXIT_SUCCESS);
        }

        my_log(log_fd, "\nRead from client_%d: \n", *counter);
        log_command(&command, log_fd);
        if (command.type == KILLSERVER)
        {
            kill(ppid, SIGINT);
            kill(current_client->pid, SIGINT);
            clean_up(client_fifo_fd_read, client_fifo_fd_write, client_connection_sem); //???
            exit(EXIT_SUCCESS);                                                         //???
        }
        else if (command.type == QUIT || signal_received)
        {
            // Set the response indicating exit
            response_t response;
            memset(&response, 0, sizeof(response));
            response.is_exit = 1;

            // Send the response to the client
            if (write(client_fifo_fd_write, &response, sizeof(response)) == -1)
            {
                perror("write");
                exit(EXIT_FAILURE);
            }

            // Close the client FIFO
            close(client_fifo_fd_read);
            close(client_fifo_fd_write);

            my_log(log_fd, "\nClient_%ld disconnected..\n", current_client->counter_id);
            sem_post(client_connection_sem);
            queue_dequeue(queue);
            if (queue_size(queue) >= max_clients)
                sem_post(free_slot_sem);
            // Terminate the server process
            exit(EXIT_SUCCESS);
        }
        else if (command.type == HELP)
        {
            response_t response;
            char *help_message = get_message(command.sub_type);
            strcpy(response.content, help_message);
            response.is_complete = 1;
            if (write(client_fifo_fd_write, &response, sizeof(response)) == -1)
            {
                if (errno == EINTR)
                {
                    clean_up(client_fifo_fd_read, client_fifo_fd_write, client_connection_sem);
                    kill(current_client->pid, SIGINT);
                    my_log(log_fd, "\nClient_%ld disconnected..\n", current_client->counter_id);
                    exit(EXIT_SUCCESS);
                }
                perror("Error while writing to client fifo");
                exit(EXIT_FAILURE);
            }
            sem_post(client_connection_sem);
        }
        else if (command.type == LIST)
        {
            response_t response;

            DIR *dir;
            struct dirent *ent;
            if ((dir = opendir(dirname)) != NULL)
            {
                // Count the number of files in the directory
                int num_files = 0;
                while ((ent = readdir(dir)) != NULL)
                {
                    if (ent->d_type == DT_REG)
                    {
                        num_files++;
                    }
                }
                closedir(dir);

                // Allocate memory for file_list
                char *file_list = (char *)malloc(num_files * (MAX_FILENAME_LENGTH + 2) * sizeof(char));
                if (file_list == NULL)
                {
                    perror("malloc");
                    exit(EXIT_FAILURE);
                }

                // Populate file_list with the filenames
                if ((dir = opendir(dirname)) != NULL)
                {
                    file_list[0] = '\0';
                    while ((ent = readdir(dir)) != NULL)
                    {
                        if (ent->d_type == DT_REG)
                        {
                            strcat(file_list, ent->d_name);
                            strcat(file_list, "\n");
                        }
                    }
                    closedir(dir);

                    // Send the response to the client in chunks of CHUNK_SIZE bytes
                    response.is_complete = 0;
                    size_t len = strlen(file_list);
                    size_t i;
                    for (i = 0; i < len; i += CHUNK_SIZE)
                    {
                        memset(response.content, 0, sizeof(response.content));
                        strncpy(response.content, file_list + i, CHUNK_SIZE);
                        if (i + CHUNK_SIZE >= len)
                        {
                            response.is_complete = 1;
                        }
                        if (write(client_fifo_fd_write, &response, sizeof(response)) == -1)
                        {
                            if (errno == EINTR)
                            {
                                clean_up(client_fifo_fd_read, client_fifo_fd_write, client_connection_sem);
                                kill(current_client->pid, SIGINT);
                                my_log(log_fd, "\nClient_%ld disconnected..\n", current_client->counter_id);
                                exit(EXIT_SUCCESS);
                            }
                            perror("write");
                            exit(EXIT_FAILURE);
                        }
                        sem_post(client_connection_sem);
                    }
                }
                else
                {
                    // Error opening directory
                    perror("opendir");
                }

                // Free the dynamically allocated memory
                free(file_list);
            }
            else
            {
                // Error opening directory
                perror("opendir");
            }
        }
        else if (command.type == READF)
        {
            response_t response;

            // Open the file
            char filepath[MAX_PATH_LENGTH];
            char file_sem_name[FILE_SEM_NAME_LEN];
            snprintf(file_sem_name, FILE_SEM_NAME_LEN, FILE_SEM_NAME_TEMPLATE, command.file);
            snprintf(filepath, MAX_PATH_LENGTH, "%s/%s", dirname, command.file);
            sem_t *sem_file = sem_open(file_sem_name, O_CREAT, 0777, 1);
            if (sem_file == SEM_FAILED)
            {
                perror("sem_open");
                exit(EXIT_FAILURE);
            }
            sem_wait(sem_file);
            int file_fd = open(filepath, O_RDONLY);
            if (file_fd == -1)
            {
                perror("open");
                exit(EXIT_FAILURE);
            }

            // Read the file chunk by chunk
            char chunk[CHUNK_SIZE];
            ssize_t bytes_read;
            int is_line_requested = command.line > 0;
            int is_complete = 0;
            size_t content_length = 0;
            while ((bytes_read = read(file_fd, chunk, sizeof(chunk))) > 0 && !is_complete)
            {
                // Check if a specific line is requested and send only that line
                if (is_line_requested)
                {
                    int line_start = 0;
                    int line_end = -1;
                    int line_number = 1;
                    for (int i = 0; i < bytes_read && !is_complete; i++)
                    {
                        if (chunk[i] == '\n')
                        {
                            line_number++;
                            if (line_number == command.line)
                            {
                                line_start = i + 1;
                            }
                            else if (line_number == command.line + 1)
                            {
                                line_end = i;
                                is_complete = 1;
                            }
                        }
                    }
                    if (line_end == -1)
                    {
                        line_end = bytes_read;
                        is_complete = 1;
                    }

                    content_length = line_end - line_start;
                    memset(response.content, 0, sizeof(response.content));
                    memcpy(response.content, chunk + line_start, content_length);
                }
                // Otherwise, send the whole file chunk by chunk
                else
                {
                    content_length = bytes_read;
                    memset(response.content, 0, sizeof(response.content));
                    memcpy(response.content, chunk, content_length);
                    if (bytes_read < (ssize_t)sizeof(chunk))
                    {
                        is_complete = 1;
                    }
                }

                // Send the response to the client
                response.is_complete = is_complete;
                if (write(client_fifo_fd_write, &response, sizeof(response)) == -1)
                {
                    if (errno == EINTR)
                    {
                        clean_up(client_fifo_fd_read, client_fifo_fd_write, client_connection_sem);
                        kill(current_client->pid, SIGINT);
                        my_log(log_fd, "\nClient_%ld disconnected..\n", current_client->counter_id);
                        exit(EXIT_SUCCESS);
                    }
                    perror("write");
                    exit(EXIT_FAILURE);
                }
                sem_post(client_connection_sem);
            }

            // Close the file descriptor
            close(file_fd);
            sem_post(sem_file);
            sem_close(sem_file);
            sem_unlink(file_sem_name);
        }
        else if (command.type == WRITET)
        {
            // Open the file
            char filepath[MAX_PATH_LENGTH];
            char file_sem_name[FILE_SEM_NAME_LEN];
            snprintf(file_sem_name, FILE_SEM_NAME_LEN, FILE_SEM_NAME_TEMPLATE, command.file);
            snprintf(filepath, MAX_PATH_LENGTH, "%s/%s", dirname, command.file);
            sem_t *sem_file = sem_open(file_sem_name, O_CREAT, 0777, 1);
            if (sem_file == SEM_FAILED)
            {
                perror("sem_open");
                exit(EXIT_FAILURE);
            }
            sem_wait(sem_file);
            int file_fd = open(filepath, O_RDWR | O_CREAT, 0777);
            if (file_fd == -1)
            {
                perror("open");
                exit(EXIT_FAILURE);
            }

            // Determine the content to write based on whether a line number is provided
            char content[MAX_WRITE_STRING_LENGTH];
            ssize_t content_length;

            if (command.line > 0)
            {
                // Find the position to insert the new line
                off_t offset = 0;
                int line_number = 1;
                ssize_t bytes_read;
                char chunk[CHUNK_SIZE];

                // Reset the file descriptor position to the beginning
                if (lseek(file_fd, 0, SEEK_SET) == -1)
                {
                    perror("lseek");
                    exit(EXIT_FAILURE);
                }
                while ((bytes_read = read(file_fd, chunk, sizeof(chunk))) > 0)
                {
                    for (ssize_t i = 0; i < bytes_read; i++)
                    {
                        if (chunk[i] == '\n')
                        {
                            line_number++;
                            if (line_number == command.line)
                            {
                                // Move the cursor to the position after the line
                                offset += i + 1;
                                break;
                            }
                        }
                    }

                    if (line_number == command.line)
                    {
                        break;
                    }

                    offset += bytes_read;
                    memset(chunk, 0, sizeof(chunk));
                }
                // Move the cursor to the line position for writing
                if (lseek(file_fd, offset, SEEK_SET) == -1)
                {
                    perror("lseek");
                    exit(EXIT_FAILURE);
                }

                // Read the old content after the line
                char old_content[MAX_WRITE_STRING_LENGTH];
                ssize_t old_content_length = read(file_fd, old_content, sizeof(old_content) - 1);
                if (old_content_length == -1)
                {
                    perror("read");
                    exit(EXIT_FAILURE);
                }
                old_content[old_content_length] = '\0';

                // Move the cursor back to the line position for overwriting
                if (lseek(file_fd, offset, SEEK_SET) == -1)
                {
                    perror("lseek");
                    exit(EXIT_FAILURE);
                }

                // Write the new string
                content_length = snprintf(content, sizeof(content), "%s\n", command.string);
                ssize_t bytes_written = write(file_fd, content, content_length);
                if (bytes_written == -1)
                {
                    if (errno == EINTR)
                    {
                        clean_up(client_fifo_fd_read, client_fifo_fd_write, client_connection_sem);
                        kill(current_client->pid, SIGINT);
                        my_log(log_fd, "\nClient_%ld disconnected..\n", current_client->counter_id);
                        exit(EXIT_SUCCESS);
                    }
                    perror("write");
                    exit(EXIT_FAILURE);
                }

                // Write the old content after the line
                bytes_written = write(file_fd, old_content, old_content_length);
                if (bytes_written == -1)
                {
                    perror("write");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                // Simply append the string to the end of the file
                content_length = snprintf(content, sizeof(content), "%s\n", command.string);
                if (lseek(file_fd, 0, SEEK_END) == -1)
                {
                    perror("lseek");
                    exit(EXIT_FAILURE);
                }
                ssize_t bytes_written = write(file_fd, content, content_length);
                if (bytes_written == -1)
                {
                    perror("write");
                    exit(EXIT_FAILURE);
                }
            }
            close(file_fd);
            sem_post(sem_file);
            sem_close(sem_file);
            sem_unlink(file_sem_name);
            // Send the response to the client indicating success
            response_t response;
            response.is_complete = 1;
            snprintf(response.content, sizeof(response.content), "Successfully written to file.\n");
            if (write(client_fifo_fd_write, &response, sizeof(response)) == -1)
            {
                if (errno == EINTR)
                {
                    clean_up(client_fifo_fd_read, client_fifo_fd_write, client_connection_sem);
                    kill(current_client->pid, SIGINT);
                    my_log(log_fd, "\nClient_%ld disconnected..\n", current_client->counter_id);
                    exit(EXIT_SUCCESS);
                }
                perror("write");
                exit(EXIT_FAILURE);
            }
            sem_post(client_connection_sem);
        }
        else if (command.type == DOWNLOAD)
        {
            response_t response;
            response.is_complete = 0;
            response.is_exit = 0;

            // Open the file for reading
            char file_path[MAX_PATH_LENGTH];

            int is_file_exist = 1;
            snprintf(file_path, sizeof(file_path), "%s/%s", dirname, command.file);
            int file_fd = open(file_path, O_RDONLY);
            if (file_fd == -1)
            {
                is_file_exist = 0;
                my_log(log_fd, "Requested file is not exist !\n");
                write(client_fifo_fd_write, &is_file_exist, sizeof(is_file_exist));
                sem_post(client_connection_sem);
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
            // Read and send the file contents in chunks
            ssize_t bytes_read;
            while (response.is_complete == 0)
            {
                bytes_read = read(file_fd, response.content, sizeof(response.content));
                if (bytes_read < (ssize_t)sizeof(CHUNK_SIZE))
                {
                    response.is_complete = 1;
                    response.content[bytes_read - 1] = '\0';
                }
                response.is_complete = (bytes_read < (ssize_t)sizeof(response.content));
                // Send the response to the client
                if (write(client_fifo_fd_write, &response, sizeof(response)) == -1)
                {
                    if (errno == EINTR)
                    {
                        printf("\nServer is closed\n");
                        close(client_fifo_fd_read);
                        close(client_fifo_fd_write);
                        exit(EXIT_SUCCESS);
                    }
                    perror("Error writing response to client");
                    break;
                }

                sem_post(client_connection_sem);
                if (response.is_complete)
                    break;

                memset(response.content, 0, sizeof(response.content));
            }

            // Close the file descriptor
            close(file_fd);
            sem_post(sem_file);
            sem_close(sem_file);
            sem_unlink(file_sem_name);
        }
        else if (command.type == UPLOAD)
        {
            // Open the file for writing
            char file_path[MAX_PATH_LENGTH], is_file_exist = 0;
            snprintf(file_path, sizeof(file_path), "%s/%s", dirname, command.file);
            if (access(file_path, F_OK) == 0)
            {
                my_log(log_fd, "File '%s' already exists. Aborting upload.\n", command.file);
                is_file_exist = 1;
            }
            write(client_fifo_fd_write, &is_file_exist, sizeof(is_file_exist));
            sem_post(client_connection_sem);
            if (is_file_exist == 1)
            {
                continue;
            }

            int file_fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
            if (file_fd == -1)
            {
                perror("Error opening file for writing");
                exit(EXIT_FAILURE);
            }

            // Read and write the file contents in chunks
            response_t response;
            ssize_t bytes_written;
            while (1)
            {
                sem_wait(client_connection_sem);
                ssize_t bytes_read = read(client_fifo_fd_read, &response, sizeof(response));
                if (bytes_read == -1)
                {
                    if (errno == EINTR)
                    {
                        printf("\nServer is closed\n");
                        close(client_fifo_fd_read);
                        close(client_fifo_fd_write);
                        exit(EXIT_SUCCESS);
                    }
                    perror("Error reading from client");
                    break;
                }
                else if (bytes_read < (ssize_t)sizeof(CHUNK_SIZE))
                {
                    response.content[bytes_read - 1] = '\0';
                }
                if (response.is_exit)
                {
                    my_log(log_fd, "\nFile upload completed.\n");
                    break;
                }
                bytes_written = write(file_fd, response.content, strlen(response.content));
                if (bytes_written == -1)
                {
                    perror("Error writing to file");
                    break;
                }
            }

            // Close the file descriptor
            close(file_fd);
            continue;
        }
    }
    return client_fifo_fd_read;
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

sem_t *connect_client_connection_sem(int client_pid)
{
    sem_t *client_connection_sem;
    char sem_name[CLIENT_SEM_NAME_LEN];
    snprintf(sem_name, CLIENT_SEM_NAME_LEN, CLIENT_SEM_NAME_TEMPLATE, (long)client_pid);
    client_connection_sem = sem_open(sem_name, O_RDWR, 0777);
    if (client_connection_sem == SEM_FAILED)
    {
        perror("Error opening client semaphore");
        exit(EXIT_FAILURE);
    }
    return client_connection_sem;
}

connection_response_t *connect_client_shm(int client_pid)
{
    connection_response_t *shm_ptr;
    int shm_fd;
    char res_shm_name[RESPOND_SHM_LEN];
    snprintf(res_shm_name, RESPOND_SHM_LEN, RESPOND_SHM_TEMPLATE, (long)client_pid);
    shm_fd = shm_open(res_shm_name, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Error opening shared memory");
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

void add_mask()
{
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGTSTP);
    if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) == -1)
    {
        perror("Error blocking signal in child process");
        exit(EXIT_FAILURE);
    }
}

void remove_mask()
{
    sigprocmask(SIG_SETMASK, &orig_mask, NULL);
}

void clean_up(int client_fifo_fd_read, int client_fifo_fd_write, sem_t *client_connection_sem)
{
    close(client_fifo_fd_read);
    close(client_fifo_fd_write);
    if (sem_close(free_slot_sem) == -1)
    {
        perror("sem_close");
        exit(EXIT_FAILURE);
    }
    if (sem_close(client_connection_sem) == -1)
    {
        perror("sem_close");
        exit(EXIT_FAILURE);
    }
}