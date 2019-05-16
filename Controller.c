#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <assert.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
 
int makeSocket(void);
int getaddr(const char *node, const char *service, struct addrinfo **address);
void updateServer(int fd, struct addrinfo *address);
void updateDashboard(int fd, struct addrinfo *address);
void getCondition(int fd, struct addrinfo *address);
void* userInputThreadController(void *arg);
void* dashThreadController(void *arg);
void* serverThreadController(void *arg);
 
 
int enginePower = 0;
int engineInc = 10;
float rcsInc = 0.1;
float rcsRoll = 0;
float fuel;
float altitude;
float previousFuel = -1;
float previousAltitude = -1;
int amountOfCommands = 0;
 
sem_t sem;
 
int main(int argc, const char *argv[]) {
    int rc = sem_init(&sem, 0, 1);
 
    if (rc != 0) {
        fprintf(stderr, "Could not create semaphore.\n");
        exit(-1);
    }
 
    /* Create all threads */
    pthread_t dashThread;
    int dt = pthread_create(&dashThread, NULL, dashThreadController, NULL);
 
    pthread_t userInputThread;
    int uit  = pthread_create(&userInputThread, NULL, userInputThreadController, NULL);
 
    pthread_t serverThread;
    int st = pthread_create(&serverThread, NULL, serverThreadController, NULL);
 
    /* Check all threads were created, if not stop program*/
    if(dt != 0) {
        fprintf(stderr, "Could not create dash thread.\n");
        exit(-1);
    }
 
    if (uit != 0) {
        fprintf(stderr, "Could not create user input thread.\n");
        exit(-1);
    }
 
    if (st != 0) {
        fprintf(stderr, "Could not create server thread.\n");
        exit(-1);
    }
 
    pthread_join(userInputThread, NULL);
}
 
void* userInputThreadController(void *arg) {
    initscr();
    noecho();
    keypad(stdscr, TRUE); //allow for arrow keys
 
    int key;
    printw("Press the vertical arrow keys to control the thrust...\n");
    printw("Press the horizontal arrow keys to control the rotational thrust...\n");
    printw("Press the ESC key twice to quit.");
 
    //if the key pressed is not the escape key
    while ((key = getch()) != 27) {
        //we can only add more power if at most 90, since max is 100
        if (key == 259 && enginePower <= 90) {
            enginePower += engineInc;
            amountOfCommands++;
        }
        else if (key == 258 && enginePower >= 10) {
            enginePower -= engineInc;
            amountOfCommands++;
        }
        else if (key == 260 && rcsRoll > -0.5) {
            rcsRoll -= rcsInc;
            amountOfCommands++;
        }
        else if (key == 261 && rcsRoll <= 0.4) {
            rcsRoll += rcsInc;
            amountOfCommands++;
        }
 
        refresh();
    }
 
    endwin();
    exit(1);
}
 
void *serverThreadController(void *arg) {
    char *port = "65200";
    char *host = "192.168.56.1";
    struct addrinfo *address;
 
    int fd;
 
    getaddr(host, port, &address);
    fd = makeSocket();
 
    while(1) {
        updateServer(fd, address);
    }
}
 
void* dashThreadController(void *arg) {
    char *dashPort = "65250";
    char *dashHost = "192.168.56.1";
    char *landerPort = "65200";
    char *landerHost = "192.168.56.1";
    struct addrinfo *dashAddress, *landerAddress;
 
    int dashSocket, landerSocket;
 
    getaddr(dashHost, dashPort, &dashAddress);
    getaddr(landerHost, landerPort, &landerAddress);
 
    dashSocket = makeSocket();
    landerSocket = makeSocket();
 
    while (1) {
        getCondition(landerSocket, landerAddress);
 
        //if the fuel or altitude has changed update the dashboard
        if(previousFuel - fuel >= 1 || previousAltitude - altitude >= 1 || fuel - previousFuel >= 1 || altitude - previousAltitude >= 1) {
            updateDashboard(dashSocket, dashAddress);
        }
    }
}
 
void updateServer(int fd, struct addrinfo *address) {
    //if we have any commands (only happens when fuel or altitude is changed)
    if(amountOfCommands > 0) {
        char outgoing[4096];
        snprintf(outgoing, sizeof(outgoing), "command:!\nmain-engine: %i\nrcs-roll: %f", enginePower, rcsRoll);
        sendto(fd, outgoing, strlen(outgoing), 0, address->ai_addr, address->ai_addrlen);
        amountOfCommands--;
    }
}
 
void updateDashboard(int fd, struct addrinfo *address) {
    const size_t buffsize = 4096;
    char outgoing[buffsize];
    snprintf(outgoing, sizeof(outgoing), "fuel: %.2f \naltitude: %.2f", fuel, altitude);
    sendto(fd, outgoing, strlen(outgoing), 0, address->ai_addr, address->ai_addrlen);
 
    int rc = sem_wait(&sem);
    assert(rc == 0);
 
    previousFuel = fuel;
    previousAltitude = altitude;
 
    rc = sem_post(&sem);
    assert(rc == 0);
}
 
void getCondition(int fd, struct addrinfo *address) {
    const size_t buffsize = 4096;
    char incoming[buffsize], outgoing[buffsize];
    size_t msgsize;
 
    strcpy(outgoing, "condition:?");
    sendto(fd, outgoing, strlen(outgoing), 0, address->ai_addr, address->ai_addrlen);
    msgsize = recvfrom(fd, incoming, buffsize, 0, NULL, 0);
    incoming[msgsize] = '\0';
 
    //get each condition
    char *condition = strtok(incoming, ":");
    char *conditions[4]; //condition returns 4 values
    int i = 0;
 
    while(condition != NULL) {
        conditions[i++] = condition;
        condition = strtok(NULL, ":");
    }
 
    //get the fuel and altitude
    char *fuelStr = strtok(conditions[2], "%");
    char *altitudeStr = strtok(conditions[3], "contact");
 
    int rc = sem_wait(&sem);
    assert(rc == 0);
 
    fuel = strtof(fuelStr, NULL);
    altitude = strtof(altitudeStr, NULL);
 
    //default, when program first starts
    if(previousFuel == -1) {
        previousFuel = fuel + 1;
    }
 
    if(previousAltitude == -1) {
        previousAltitude = altitude + 1;
    }
 
    rc = sem_post(&sem);
    assert(rc == 0);
}
 
int getaddr(const char *node, const char *service, struct addrinfo **address) {
    struct addrinfo hints = {
        .ai_flags = 0,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM
    };
 
    if(node)
        hints.ai_flags = AI_CANONNAME;
    else
        hints.ai_flags = AI_PASSIVE;
 
    int err = getaddrinfo(node, service, &hints, address);
 
    if(err) {
        fprintf(stderr, "ERROR Getting Address: %s\n", gai_strerror(err));
        exit(1);
        return false;
    }
 
    return true;
}
 
int makeSocket(void) {
    int sfd = socket(AF_INET, SOCK_DGRAM, 0);
 
    if(sfd == -1) {
        fprintf(stderr, "Error making socket: %s\n", strerror(errno));
        exit(1);
        return 0;
    }
 
    return sfd;
}
 
int bindSocket(int sfd, const struct sockaddr *addr, socklen_t addrlen) {
    int err = bind(sfd, addr, addrlen);
 
    if(err == -1) {
        fprintf(stderr, "Error Binding socket: %s\n", strerror(errno));
        return false;
    }
 
    return true;
}