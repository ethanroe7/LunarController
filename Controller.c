#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ncurses.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
 
void* userInputThreadController(void *arg);
void* dashboardThreadController(void *arg);
void* serverThreadController(void *arg);
void sendCommand(int fd, struct addrinfo *address);
void getCondition(int fd, struct addrinfo *address);
void getUserInput(int fd, struct addrinfo *address);
void dashUpdate(int fd, struct addrinfo *address);
void serverUpdate(int fd, struct addrinfo *address);
int getaddr(const char *node, const char *service, struct addrinfo **address);
int makeSocket(void);
 
float rcsInc = 0.1;
float rcsRoll = 0;
int landerEnginePower = 0;
int landerEngineInc = 10;
char landerFuel;
char landerAltitude;
char fuelBefore;
char altitudeBefore;
int commands = 0;
 
int main(int argc, const char **argv) {

    pthread_t dashboardThread;
    int dthread = pthread_create(&dashboardThread, NULL, dashboardThreadController, NULL);
 
    pthread_t userInputThread;
    int uithread  = pthread_create(&userInputThread, NULL, userInputThreadController, NULL);

    pthread_t serverThread;
    int sthread = pthread_create(&serverThread, NULL, serverThreadController, NULL);
 
    if(dthread != 0) {
        fprintf(stderr, "Could not create thread.\n");
        exit(-1);
    }
 
    if (uithread != 0) {
        fprintf(stderr, "Could not create thread.\n");
        exit(-1);
    }

     if (sthread != 0) {
        fprintf(stderr, "Could not create thread.\n");
        exit(-1);
    }
 }
 
void* userInputThreadController(void *arg) {
    char *port = "65200";
    char *host = "192.168.56.1";
    struct addrinfo *address;
    int fd;
    getaddr(host, port, &address);
    fd = makeSocket();
    getUserInput(fd, address);
    exit(0);
}
 
void* dashboardThreadController(void *arg) {
    char *dashboardPort = "65250";
    char *dashboardHost = "192.168.56.1";
    char *lunarLanderPort = "65200";
    char *lunarLanderHost = "192.168.56.1";
    struct addrinfo *dashboardAddress, *lunarLanderAddress;
    int dashboardSocket, lunarLanderSocket;
 
    getaddr(dashboardHost, dashboardPort, &dashboardAddress);
    getaddr(lunarLanderHost, lunarLanderPort, &lunarLanderAddress);
    dashboardSocket = makeSocket();
    lunarLanderSocket = makeSocket();
 
    while (1) {
        getCondition(lunarLanderSocket, lunarLanderAddress);
        dashUpdate(dashboardSocket, dashboardAddress);
    }
}

void* serverThreadController(void *arg) {
    char *port = "65200";
    char *host = "192.168.56.1";
    struct addrinfo *address;
    int fd;
    getaddr(host, port, &address);
    fd = makeSocket();
    while(1) {
        serverUpdate(fd, address);
    }
}
 
void getUserInput(int fd, struct addrinfo *address) {
    initscr();
    noecho();
    keypad(stdscr, TRUE); 
    int key;
    printw("To control the thrust, press the vetical arrow keys...\n");
    printw("To control the rotational thrust, press the horizontal arrow keys ...\n");
    printw("To quit, press ESC.");
 
    while((key=getch()) != 27) {
        move(10, 0);
        if(key == 259 && landerEnginePower <= 90) {
            landerEnginePower += landerEngineInc;
            commands++;
        }
        else if(key == 258 && landerEnginePower >= 10) {
            landerEnginePower -= landerEngineInc;
            commands++;
        }
        else if(key == 260 && rcsRoll > -0.5) {
            rcsRoll -= rcsInc;	
            commands++;
        }
        else if(key == 261 && rcsRoll <= 0.4) {
            rcsRoll += rcsInc;
            commands++;
        }
 
        move(0, 0);
        refresh();
    }
    endwin();
    exit(1);
}
 
void sendCommand(int fd, struct addrinfo *address) {
    const size_t buffsize = 4096;
    char outgoing[buffsize];
 
    snprintf(outgoing, sizeof(outgoing), "command:!\nmain-engine: %i\nrcs-roll: %f", landerEnginePower, rcsRoll);
    sendto(fd, outgoing, strlen(outgoing), 0, address->ai_addr, address->ai_addrlen);
}
 
void dashUpdate(int fd, struct addrinfo *address) {
    const size_t buffsize = 4096;
    char outgoing[buffsize];
    snprintf(outgoing, sizeof(outgoing), "fuel: %s \naltitude: %s", landerFuel, landerAltitude);
    sendto(fd, outgoing, strlen(outgoing), 0, address->ai_addr, address->ai_addrlen);

    fuelBefore = landerFuel;
    altitudeBefore = landerAltitude; 
}

void serverUpdate(int fd, struct addrinfo *address) {
    if(commands > 0) {
        char outgoing[4096];
        snprintf(outgoing, sizeof(outgoing), "command:!\nengine: %i\nrcs-roll: %f", landerEnginePower, rcsRoll);
        sendto(fd, outgoing, strlen(outgoing), 0, address-> ai_addr, address->ai_addrlen);
        commands--;
    }
}
 
void getCondition(int fd, struct addrinfo *address) {
    const size_t buffsize = 4096; //4k
    char incoming[buffsize], outgoing[buffsize];
    size_t msgsize;
 
    strcpy(outgoing, "condition:?");
    sendto(fd, outgoing, strlen(outgoing), 0, address->ai_addr, address->ai_addrlen);
    msgsize = recvfrom(fd, incoming, buffsize, 0, NULL, 0);
    incoming[msgsize] = '\0';
 
    char *condition = strtok(incoming, ":");
    char *conditions[4]; //condition returns 4 values
    int i = 0;
 
    while(condition != NULL) {
        conditions[i++] = condition;
        condition = strtok(NULL, ":");
    }

    char *landerFuelStr = strtok(conditions[2], "%");
    char *landerAltitudeStr = strtok(conditions[3], "contact");
    
    landerFuel = strtof(landerFuelStr, NULL);
    landerAltitude = strtof(landerAltitudeStr, NULL);

    if(fuelBefore == -1) {
	fuelBefore = landerFuel +1;
    }

    if(altitudeBefore == -1) {
	altitudeBefore = landerAltitude +1;
    }
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
        fprintf(stderr, "Error: couldn't get address: %s\n", gai_strerror(err));
        exit(1);
        return false;
    }
    return true;
}
 
int makeSocket(void) {
    int sfd = socket(AF_INET, SOCK_DGRAM, 0); 
    if(sfd == -1) {
        fprintf(stderr, "Error: couldn't make socket: %s\n", strerror(errno));
        exit(1);
        return 0;
    }
    return sfd;
}
 
int bindSocket(int sfd, const struct sockaddr *addr, socklen_t addrlen) {
    int err = bind(sfd, addr, addrlen);
    if(err == -1) {
        fprintf(stderr, "Error: couldn't bind socket: %s\n", strerror(errno));
        return false;
    }
    return true;
}