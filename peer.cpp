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

int main(int argc, char* argv[]) 
{	
  // PEER
  // ./peer <my-ip> <tracker-ip> <input-file> <owned-chunks> <output-file> <log> 
  args peerArgs = retrieveArgs(argv);
  
  return 0;
}
