#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <iostream>
#include <cstring>
#include <fstream>

#include "PacketHeader.h"
#include "crc32.h"

#define TRACKERPORT 6969

using namespace std; 

struct args {
  char* myIP;
  char* trackerIP;
  char* inputFile;
  char* ownedChunks;
  char* outputFile;
  char* log;
};

auto retrieveArgs(char* argv[]) {
  args newArgs;
  newArgs.myIP = argv[1];
  newArgs.trackerIP = argv[2];
  newArgs.inputFile = argv[3];
  newArgs.ownedChunks = argv[4];
  newArgs.outputFile = argv[5];
  newArgs.log = argv[6];
  return newArgs;
}

void connectToTracker(char* myIP, char* trackerIP) {
  // create socket
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }
  // connect to the tracker's binded socket
  struct sockaddr_in tracker_addr;
  tracker_addr.sin_family = AF_INET;
  tracker_addr.sin_addr.s_addr = INADDR_ANY;
  tracker_addr.sin_port = htons((u_short) TRACKERPORT);
  struct hostent *tracker_host = gethostbyname(trackerIP);
  if (tracker_host == NULL) {
    perror("ERROR, no such host");
    exit(1);
  }
  memcpy(&tracker_addr.sin_addr, tracker_host->h_addr_list[0], tracker_host->h_length);
  if (connect(sockfd, (struct sockaddr *) &tracker_addr, sizeof(tracker_addr)) < 0) {
    perror("ERROR connecting");
    exit(1);
  }
  cout << "Connected to tracker" << endl;

  // generate random number
  srand(time(NULL));
  int randomNum = rand() % 1000000;
  // create file with random number
  ofstream randomFile;
  string randomFilePath = "randomFile" + to_string(randomNum) + ".txt";
  randomFile.open(randomFilePath);
  randomFile << randomNum << endl;
}

int main(int argc, char* argv[]) 
{	
  // PEER
  // ./peer <my-ip> <tracker-ip> <input-file> <owned-chunks> <output-file> <log> 
  args peerArgs = retrieveArgs(argv);

  connectToTracker(peerArgs.myIP, peerArgs.trackerIP);
  return 0;
}
