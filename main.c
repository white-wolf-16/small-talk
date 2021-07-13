#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>

#include "list.h"

#define MSG_MAX_LEN 1024

typedef struct Socket {
    pthread_cond_t condition;
    pthread_mutex_t mutex;
    List* messages;
} Socket;

static struct Socket received; // global ListStruct for recieved 
static struct Socket sent; // global ListStruct for send 

int sockfd; // socket
static struct addrinfo* remoteAddr;

// Input threads
pthread_t keyboardThread;
pthread_t senderThread;
pthread_t receiverThread;
pthread_t screenThread;

bool endChat;
char* message; char* messageRx;


// Helper fn
static void cleanUpMem() {
    List_free(sent.messages, &free);
    pthread_mutex_destroy(&sent.mutex);

    List_free(received.messages, &free);
    pthread_mutex_destroy(&received.mutex);

    pthread_cond_destroy(&received.condition);
    pthread_cond_destroy(&sent.condition);

    close(sockfd);
}

// Thread fn
void* keyboard(void* msg){

    while(1){
        if (fgets(message, MSG_MAX_LEN, stdin) != NULL) {; // gets user's keyboard input
            pthread_mutex_lock(&sent.mutex); // Condition of accesing CS locked to keyboardSocket
            
            // if send list is full, wait till there is space
            if(List_count(sent.messages) == LIST_MAX_NUM_NODES){
                printf("Cannot read message from keyboard, List is full\n");
                pthread_cond_wait(&sent.condition, &sent.mutex);
            }
    
            // add message to the list
            if (List_append(sent.messages, message) == -1){  
                printf("List append failed");
            }
            pthread_cond_signal(&sent.condition); // signal something has been added to the list
            pthread_mutex_unlock(&sent.mutex); // CS done
                
            if(endChat == true){
                break;
            } 

            // check for end chat message input of !
            if(message[0] == '!' && message[1] == '\n'){
                break;
            }   
        }   
    }
    printf("Keyboard exited\n");
    pthread_exit(NULL);
}

void* sender(void* unused){
    
    while(1){
        pthread_mutex_lock(&sent.mutex); // condition locked to senderSocket for sending message to remote
        
        // if list is empty, wait till something is added to it
        if(List_count(sent.messages) == 0){
            pthread_cond_wait(&sent.condition, &sent.mutex);
        }
        
        char* message = (char*)List_trim(sent.messages); // trim the last message from the list
        
        if(endChat == true){
            free(message);
            break;
        }
        
        pthread_cond_signal(&sent.condition); // signal items present in the list
        pthread_mutex_unlock(&sent.mutex); // CS done
        
        if(sendto(sockfd, message, MSG_MAX_LEN, 0, 
            remoteAddr->ai_addr, remoteAddr->ai_addrlen) < 0 ){

            printf("Error sending message %s\n", message);
        }

        // check for end chat message input of !
        if(message[0] == '!' && message[1] == '\n'){
            endChat = true;
            printf("Terminating......\n"); sleep(.7);
            // ** needs to be linked with receiver
            // pthread_cond_signal(&received.condition);     // ending chat signal the waiting threads nothing is coming
            pthread_cancel(receiverThread);                  // cancel receiver thread
            pthread_cancel(screenThread);                    // cancel screen thread
            printf("Receiver exited\n");
            printf("Screen exited\n");

            free(messageRx);
            free(message);
            cleanUpMem();
            break;
        }
    }
    printf("Sender exited\n");
    pthread_exit(NULL);
}

void* receiver(void* unused){

    int bytesRx;

    while(1){

        //recv
        bytesRx = recvfrom(sockfd, messageRx, MSG_MAX_LEN, 0,
                    remoteAddr->ai_addr, &remoteAddr->ai_addrlen);

        //recv check
        if (bytesRx == -1 ){
            perror("receiver error: recvfrom\n"); exit (1);
        }
        
        //slaming a terminating char
        int terminateIdx = (bytesRx < (uintptr_t)messageRx) ? bytesRx : MSG_MAX_LEN - 1;
        messageRx[terminateIdx] = 0;

        pthread_mutex_lock(&received.mutex);
        {   
            //inserting to list
            if(List_count(received.messages) < LIST_MAX_NUM_NODES){
                if (List_prepend(received.messages, messageRx) == -1)
                    printf("List prepend failed");
            }
            else{
                perror("receiver error: nodes exhausted\n"); exit(1);
            }

            //wake up screen
            if(List_count(received.messages) > 0)
                pthread_cond_signal(&received.condition);

            //sleep if no data is streamed
            pthread_cond_wait(&received.condition, &received.mutex);
        }
        pthread_mutex_unlock(&received.mutex);

        if(messageRx[0] == '!' && messageRx[1] == '\n'){
            endChat = true;
            printf("Terminating......\n"); sleep(.7);
            pthread_cancel(keyboardThread);                 // cancel keyboard thread
            pthread_cancel(senderThread);                   // cancel sender thread
            pthread_cancel(screenThread);                   // cancel screen thread
            printf("Keyboard exited\n");
            printf("Sender exited\n");
            printf("Screen exited\n");
            free(message);
            cleanUpMem();
            break;
        }
    }
    free(messageRx);
    printf("Receiver exited\n");
    pthread_exit(NULL);
}

void* screen(void* unused){
    while(1){
        
        pthread_mutex_lock(&received.mutex);
        {   
            if (received.messages == NULL)
                break;
            //sleep if list is empty
            if(List_count(received.messages) <= 0){
                pthread_cond_wait(&received.condition, &received.mutex);
            }
            //print to screen and trim
            printf("Recieved: %s", (char*)List_trim(received.messages));
            //wake up receiver
            pthread_cond_signal(&received.condition);
        }
        pthread_mutex_unlock(&received.mutex);
    }
    printf("Screen exited\n");
    pthread_exit(NULL);
}

// Initialization
void Sender_init(void* msg){
    message = msg;
    pthread_create(&senderThread, NULL, sender, NULL);
}
void Receiver_init(void* msg){
    messageRx = msg;
    pthread_create(&receiverThread, NULL, receiver, NULL); 
}

// Main
int main (int argc, char **argv){

    if(argc != 4){
        printf("Need 4 parameters\n");
        printf("usage: s-talk [my port number] [remote machine name] [remote port number]\n");
        return -1;
    }

    // extract parameters and convert ports to int
    int myPort = atoi(argv[1]);
    char* remoteName = argv[2];
    int remotePort = atoi(argv[3]);
    char* charRemotePort = argv[3];

    //malloc
    char* message = malloc(MSG_MAX_LEN * sizeof(char));
    char* messageRx = malloc(MSG_MAX_LEN * sizeof(char));
    //malloc check
    if (message == NULL || messageRx == NULL) {
        printf("Keyboard malloc failed");
    }

    // check port numbers are >1024 and <65535
    if(myPort < 1024 || myPort > 65535){
        printf("Invalid local port number\n");
        return -1;
    }
    if(remotePort < 1024 || remotePort > 65535){
        printf("Invalid remote port number\n");
        return -1;
    }

    // https://coursys.sfu.ca/2021su-cmpt-300-d1/pages/tut_sockets/view
    // my socket
    struct sockaddr_in socketAddr; 
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == 0){ 
        printf("Socket creation failed\n");
        return -1;
    }
    
    // socket info
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = htons(myPort);
    socketAddr.sin_addr.s_addr = INADDR_ANY;
    memset(&socketAddr.sin_zero, '\0', 8);
    
    // bind socket to myPort
    if (bind(sockfd, (struct sockaddr *)&socketAddr, sizeof(struct sockaddr_in))<0){ 
        printf("Socket binding failed\n");
        close(sockfd);
        return -1; 
    }
    
    // socket to send messages 
    static struct addrinfo hints;
    memset(&hints, '\0', sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    
    // get remote address
    if(getaddrinfo(remoteName, charRemotePort, &hints, &remoteAddr) != 0){
        printf("Getting remote address failed\n");
        close(sockfd);
        return -1;
    }

    // initiate recieved condition
    if(pthread_cond_init(&received.condition, NULL) != 0){
        printf("Initializing received condition failed\n");
        close(sockfd); // close socket
        return -1;
    }
    
    // initiate sent condition 
    if(pthread_cond_init(&sent.condition, NULL) != 0){
        printf("Initializing sent condition failed\n");
        pthread_cond_destroy(&received.condition);
        close(sockfd); // close socket
        return -1;
    }

    // initiate mutex for recieved list
    if(pthread_mutex_init(&received.mutex, NULL) != 0){
        printf("Initializing received mutex failed\n");
        List_free(received.messages, &free);
        close(sockfd); // close socket
        pthread_cond_destroy(&received.condition);
        pthread_cond_destroy(&sent.condition);
        return -1;
    }
    // create sent list
    sent.messages = List_create();
    if(sent.messages == NULL){
        printf("Creating sent list failed\n");
        List_free(received.messages, &free);
        close(sockfd); // close socket
        pthread_cond_destroy(&received.condition);
        pthread_cond_destroy(&sent.condition);
        return -1;
    }
    // initiate mutex for sent list
    if(pthread_mutex_init(&sent.mutex, NULL) != 0){
        printf("Initializing sent mutex failed\n");
        List_free(received.messages, &free);
        List_free(sent.messages, &free);
        close(sockfd); // close socket
        pthread_cond_destroy(&received.condition);
        pthread_cond_destroy(&sent.condition);
        return -1;
    }
    endChat = false;

    // create recieve list
    received.messages = List_create();
    if(received.messages == NULL){
        printf("Creating recieve list failed\n");
        List_free(received.messages, &free);
        close(sockfd); // close socket
        pthread_cond_destroy(&received.condition);
        pthread_cond_destroy(&sent.condition);
        return -1;
    }
    
    // create threads 
    pthread_create(&keyboardThread, NULL, keyboard, NULL); 
    Sender_init(message);
    Receiver_init(messageRx); 
    pthread_create(&screenThread, NULL, screen, NULL); 

    //join threads 
    pthread_join(keyboardThread, NULL);
    pthread_join(senderThread, NULL);
    pthread_join(receiverThread, NULL);
    pthread_join(screenThread, NULL);

    // free addrinfo
    freeaddrinfo(remoteAddr);

    printf("End of Program!\n");

    return 0;
}


