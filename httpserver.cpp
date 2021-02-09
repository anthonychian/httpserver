// Anthony Chian
// CSE130
// Assignment 3 -  multithreaded HTTP server with logging and caching
// December 5, 2019

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <err.h>
#include <arpa/inet.h>
#include <pthread.h>
#define CAPACITY 4

int new_socket;
int offset = 0;
int numPages = 0;
bool logging = false;
bool caching = false;
const char *logfile;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t socketMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t offsetMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t threadCondition = PTHREAD_COND_INITIALIZER;
pthread_cond_t socketCondition = PTHREAD_COND_INITIALIZER;

struct threadInfo
{
    int index;
    int socket;
};

struct Node {
    char *file;
    char *content;
    struct Node *next;
};

struct Node *head = NULL;
struct Node *current = NULL;

// function to print contents of linked list
// used for testing purposes only
void printCache() {
    struct Node *temp = head;
    if (head == NULL)
        printf("\n* ======= CACHE IS EMPTY======= *\n");
    else {
        printf("\n* ======= PRINTING CURRENT CACHE ======= *\n");
        while (temp != NULL) {
            printf("\nFile:%s\nContent:%s\n",temp->file, temp->content);
            temp = temp->next;
        }
        printf("\n* ====================================== *\n");
    }
}

// remove oldest node in linkedlist
// frees memory of old node
void removeFirst() {
    if (head != NULL) {
        struct Node *temp = head;
        head = head->next;
        free(temp);
        numPages--;
    }
}

// creates new node with filename and file content
// insert new node at the end of linkedlist
void addNode(char *filename, char *data) {
    struct Node *new_node = (struct Node*) malloc(sizeof(struct Node));
    // allocate new memory for filename and content of file
    new_node->file = (char*)malloc(sizeof(char) * strlen(filename)+1);
    strcpy(new_node->file, filename);
    new_node->content = (char*)malloc(sizeof(char) * strlen(data)+1);
    strcpy(new_node->content, data);
    new_node->next = NULL;
    numPages++;

    if (head == NULL) {
        head = new_node;
        return;
    }
    current = head;
    // add node to the end of the linked list
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = new_node;
}

// inserts a page to the cache if not at CAPACITY,
// or updates the cache if the page is already present
// assumes that the cache can be empty/full/partially full
// returns true if page was updated in cache and false if page was added to cache
bool updateCache(char *filename, char *data) {
    // if cache is empty add new page
    if (head == NULL) {
      addNode(filename, data);
      //printf("\nAdded new page to empty cache\n");
      return false;
    }
    current = head;
    // search for an instance of the page in the cache
    while (current != NULL) {
        if (strcmp(current->file, filename) == 0) {
            current->content = data;
            //printf("\nUpdated existing page in cache\n");
            return true;
        }
        current = current->next;
    }
    // Remove the oldest page (page at the head of linked list)
    if (numPages == CAPACITY) {
        removeFirst();
        //printf("\nRemoved oldest page in cache\n");
    }
    // if page is not in cache add a new page
    addNode(filename, data);
    //printf("\nAdded new page to cache\n");
    return false;
}
// reads a page from cache if present (if filename matches any in the cache)
// returns content from file if present or "none" if not found
char *readFromCache(char *filename) {
    if (head == NULL)
        return (char*)"none";

    current = head;
    while (current != NULL) {
        if (strcmp(current->file, filename) == 0) {
            //printf("\nFound file to read from cache\n");
            return (char*)current->content;
        }
        current = current->next;
    }
    return (char*)"none";
}

bool isValid(char *filename) {
    if (strlen(filename) != 27)
        return false;
    for (int i = 0; i < strlen(filename); i++) {
        if (isalpha(filename[i]) == 0 && isdigit(filename[i]) == 0 && filename[i] != '-' && filename[i] != '_')
            return false;
    }
    return true;
}

void cat(int fd, char *s, int n_socket) {
    char buf[4000];
    char response[100];
    ssize_t numBytes;
    // Keep reading from file descriptor until there are no bytes left in file
    while ((numBytes = read(fd, buf, (size_t)sizeof buf)) > 0) {
        sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\n%s", strlen(buf), buf);
        write(n_socket, response, strlen(response));
    }
    if (numBytes < 0)
        err(1, "%s", s);

}

void processFAIL(char *filename, const char *logfile, const char *method, int response) {
    char info[100];
    int threadbuffer1;
    sprintf(info, "FAIL: %s %s HTTP/1.1 --- response %d\n========\n", method, filename, response);
    int fd = open(logfile, O_WRONLY | O_APPEND);
    if (fd > 0) {
        // reserve space to write to the logfile
        pthread_mutex_lock(&offsetMutex);
        threadbuffer1 = offset;
        offset += strlen(info);
        pthread_mutex_unlock(&offsetMutex);
        // writes to the logfile at an offset
        pwrite(fd, info, strlen(info), threadbuffer1);
        close(fd);
    }
}

void processGET(char *filename, int n_socket, const char *logfile) {
    int fd;
    char response[100];
    if (access(filename, F_OK ) == -1) {
        sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Length: 0");
        write(n_socket, response, strlen(response));
        if (logging) {
            processFAIL(filename, logfile, "GET", 404);
        }
    }
    else {
        bool found = false;
        if (caching) {
            char *buf = readFromCache(filename);
            // read the page from the cache
            if (strcmp((char*)"none", buf) != 0 ) {
                found = true;
                char response[4000];
                sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\n%s", strlen(buf), buf);
                write(n_socket, response, strlen(response));
                //printf("\nSuccessful read from cache\n");
                //printCache();
            }
        }
        fd = open(filename, O_RDONLY);
        if (fd > 0) {
            // read the page from disk
            if (!caching || (caching && !found)) {
                cat(fd, filename, n_socket);
                close(fd);
                //printf("\nRead from disk/not in cache\n");
                //printCache();
            }
            if (logging) {
                char info[100];
                int threadbuffer1;
                if (found)
                    sprintf(info, "GET %s length 0 [was in cache]\n========\n", filename);
                else
                    sprintf(info, "GET %s length 0 [was not in cache]\n========\n", filename);
                fd = open(logfile, O_WRONLY | O_APPEND);
                if (fd > 0) {
                    // reserve space to write to the logfile
                    pthread_mutex_lock(&offsetMutex);
                    threadbuffer1 = offset;
                    offset += strlen(info);
                    pthread_mutex_unlock(&offsetMutex);
                    // writes to the logfile at an offset
                    pwrite(fd, info, strlen(info), threadbuffer1);
                    close(fd);
                }
            }
        }
        else {
            sprintf(response, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0");
            write(n_socket, response, strlen(response));
            if (logging) {
                processFAIL(filename, logfile, "GET", 403);
            }
        }
    }
}

void processPUT(char *filename, int n_socket, const char *logfile) {
    char data[3800];
    char response[100];
    int fd;
    ssize_t numBytes;

    numBytes = recv(n_socket, data, sizeof data, 0);

    bool updated;
    if (caching) {
        updated = updateCache(filename, (char*)data);
        //printCache();
    }

    if (access(filename, F_OK ) == -1)
        sprintf(response, "HTTP/1.1 201 Created\r\nContent-Length: 0");
    else
        sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: 0");
    write(n_socket, response, sizeof response);

    // writes contents to the specified filename
    fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT,
    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd > 0) {
        // while ((numBytes = recv(n_socket, data, sizeof data, 0)) > 0) {
        //
        // }
        write(fd, data, strlen(data));
        close(fd);
        // logging is only done if "-l" is specified
        if (logging) {
            char info[100];
            char hex[12000];
            int threadbuffer1, threadbuffer2;

            int ncount = 0;
            int length = 0;

            // translate content into hex byte by byte
            for (int k = 0; k < strlen(data);k++) {
                if (k % 20 == 0) {
                    length += sprintf(hex+length, "\n%07d %02X ", ncount, data[k]);
                    ncount += 20;
                }
                else
                    length += sprintf(hex+length, "%02X ",data[k]);
            }
            length += sprintf(hex+length, "\n========\n");

            // create PUT logging header line
            if (updated)
                sprintf(info, "PUT %s length %lu [was in cache]", filename, numBytes);
            else
                sprintf(info, "PUT %s length %lu [was not in cache]", filename, numBytes);

            fd = open(logfile, O_WRONLY | O_APPEND);

            if (fd > 0) {
                // reserve space to write to the logfile
                pthread_mutex_lock(&offsetMutex);
                threadbuffer1 = offset;
                offset += strlen(info);
                threadbuffer2 = offset;
                offset += strlen(hex);
                pthread_mutex_unlock(&offsetMutex);

                // writes to the logfile at an offset
                pwrite(fd, info, strlen(info), threadbuffer1);
                pwrite(fd, hex, strlen(hex), threadbuffer2);

                close(fd);
            }
        }

    }
    else {
        sprintf(response, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0");
        write(n_socket, response, strlen(response));
        if (logging) {
            processFAIL(filename, logfile, "PUT", 403);
        }
    }
}

void dispatcher(int NTHREADS, const char *hostname, const int PORT, threadInfo thr[]) {

    struct sockaddr_in address;
    int server_fd, fd;
    int addrlen = sizeof(address);
    int i;
    bool sent;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("\nServer cannot create socket. \n");
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;
    inet_aton(hostname, &address.sin_addr);

    memset(address.sin_zero, '\0', sizeof address.sin_zero);

    if (bind(server_fd,(struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("\nBind in server failed. \n");
    }

    if (listen(server_fd, 5) < 0) {
        perror("\nListen error. \n");
        exit(EXIT_FAILURE);
    }
    while (1) {
        printf("\nWaiting to accept message...\n");

        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("\nAccept error. \n");
            exit(EXIT_FAILURE);
        }

        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&threadCondition);
        pthread_mutex_unlock(&mutex);

        pthread_mutex_lock(&socketMutex);
        while (new_socket != -1) {
            pthread_cond_wait(&socketCondition, &socketMutex);
        }
        pthread_mutex_unlock(&socketMutex);

    }

}

void *workerThread(void *threadarg) {
    struct threadInfo *data;
    data = (struct threadInfo *) threadarg;
    while(1) {
        pthread_mutex_lock(&mutex);

        // woker thread waits until signaled
        pthread_cond_wait(&threadCondition, &mutex);

        pthread_mutex_unlock(&mutex);

        //printf("THREAD:%d IS NOW WORKING\n", data->index);

        pthread_mutex_lock(&socketMutex);
        data->socket = new_socket;
        new_socket = -1;
        pthread_cond_signal(&socketCondition);
        pthread_mutex_unlock(&socketMutex);

        char *method, *filename, *protocol;
        char buffer[4000];
        char response[100];
        recv(data->socket, buffer, sizeof buffer, 0);

        printf("%s", buffer);

        method = strtok(buffer, " ");
        filename = strtok(NULL, " ");

        if (!isValid(filename)) {
            sprintf(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0");
            write(data->socket, response, strlen(response));
            if (logging) {
                processFAIL(filename, logfile, method, 400);
            }
        }
        else if (strcmp(method, "GET") != 0 && strcmp(method, "PUT") != 0) {
            sprintf(response, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0");
            write(data->socket, response, strlen(response));
            if (logging) {
                processFAIL(filename, logfile, method, 500);
            }
        }
        else {
            if (strcmp(method, "GET") == 0) {
                processGET(filename, data->socket, logfile);
            }
            else if (strcmp(method, "PUT") == 0) {
                processPUT(filename, data->socket, logfile);
            }
        }
        close(data->socket);

        //printf("THREAD:%d IS NOW DONE\n", data->index);
    }
}

int main(int argc, char const *argv[]) {
    //specify hostname and PORT in the commandline
    const char *hostname = argv[1];
    const int PORT = atoi(argv[2]);

    // caching is false by default
    // and only true if "-c" is specified
    for (int j = 0; j < argc; j++) {
        if (strcmp(argv[j], "-c") == 0)
            caching = true;
    }

    // logging is false by default
    // and only true if "-l" is specified followed by the filename (ex: "-l logfile.txt")
    for (int j = 0; j < argc; j++) {
        if (strcmp(argv[j], "-l") == 0) {
            logging = true;
            logfile = argv[j+1];
        }
    }

    // server creates NTHREADS, 4 by default
    // and only changes if "-N" is specified followed by a number (ex: "-N 6")
    int NTHREADS = 4;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-N") == 0)
            NTHREADS = atoi(argv[i+1]);
    }

    pthread_t thread_id[NTHREADS];

    struct threadInfo thr[NTHREADS];


    //start all threads but have them all sleep from start
    for (int i = 0; i < NTHREADS; i++) {
        thr[i].index = i;
        thr[i].socket = 0;
        pthread_create(&thread_id[i], NULL, workerThread, (void*)&thr[i]);
    }

    dispatcher(NTHREADS, hostname, PORT, thr);

    return 0;
}
