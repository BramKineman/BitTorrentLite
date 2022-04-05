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
  char* peerList;
  char* inputFile;
  char* torrentFile;
  char* log;
};

auto retrieveArgs(char* argv[]) {
  args newArgs;
  newArgs.peerList = argv[1];
  newArgs.inputFile = argv[2];
  newArgs.torrentFile = argv[3];
  newArgs.log = argv[4];
  return newArgs;
}

void readPeerListToTorrentFile(char* &peerList) {
  ifstream peerListFile(peerList);
  string line;
  ofstream torrentFile;
  torrentFile.open("torrent.txt");
  while (getline(peerListFile, line)) {
    torrentFile << line << endl;
  }
}

int main(int argc, char* argv[]) 
{	
  // TRACKER
  // ./tracker <peers-list> <input-file> <torrent-file> <log> 
  args trackerArgs = retrieveArgs(argv);

  // creates torrent files by...
  // reading from peers-list.txt  <-- contains IPs of peers. Manually created file, trackerArgs.peerList is a path to the file
  // reading from input-file.txt   <-- contains the file to be 'downloaded'. Manually created file, trackerArgs.inputFile is a path to the file.
  // chunk inputFile into pieces of size CHUNK_SIZE
  readPeerListToTorrentFile(trackerArgs.peerList);
  
  // create a torrent file torrent.txt
  // X <-- number of peers
  // 10.0.0.1 <-- peer IP 1
  // 10.0.0.2 <-- peer IP 2
  // Y <-- number of chunks
  // 0 4314141516 <-- chunk 0 and crc of chunk 0
  // 1 8345435731 <-- chunk 1 and crc of chunk 1

  // distributes torrent files to any peer that connects

  return 0;
}
