#include "../include/logger.h"

int create_log_file(const char *dirname)
{
    // Create logs directory if it doesn't exist
    char logs_dir[MAX_PATH_LENGTH - sizeof(SERVER_FIFO_TEMPLATE)];
    snprintf(logs_dir, sizeof(logs_dir), "%s/%s", dirname, "logs");
    if (mkdir(logs_dir, 0777) == -1 && errno != EEXIST)
    {
        perror("Error creating logs directory");
        exit(1);
    }

    // Create log file
    char log_path[MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/%s_%ld", logs_dir, SERVER_LOG_FILE_NAME, (long)getpid());
    int log_fd = open(log_path, O_CREAT | O_WRONLY | O_APPEND | O_TRUNC, 0777);
    if (log_fd < 0)
    {
        perror("Error creating server log file");
        exit(1);
    }
    return log_fd;
}

void my_log(int log_fd, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    size_t len = strlen(message);
    write(log_fd, message, len);
    write(STDOUT_FILENO, message, len);
}