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
#include <vector>

#include "PacketHeader.h"
#include "crc32.h"

#define FILE_CHUNK_SIZE 512000

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

void readPeerListToTorrentFile(char* &peerList, char* &torrentFile) {
  ifstream peerListFile(peerList);
  string line;
  vector<string> peerListVector;
  ofstream torrentFileStream;
  torrentFileStream.open(torrentFile);
  while (getline(peerListFile, line)) {
    peerListVector.push_back(line);
  }
  torrentFileStream << peerListVector.size() << endl;
  for (unsigned int i = 0; i < peerListVector.size(); i++) {
    torrentFileStream << peerListVector[i] << endl;
  }
}

void readInputFileToTorrentFile(char* &inputFile, char* &torrentFile) {
  int chunk = 0;
  ifstream inputFileStream(inputFile, ios::binary);
  string line;
  vector<string> fileChunks;
  char buffer[FILE_CHUNK_SIZE];

  while (!inputFileStream.eof()) {
    inputFileStream.read(buffer, FILE_CHUNK_SIZE);
    // run crc32 on the chunk
    unsigned int crc = crc32(buffer, FILE_CHUNK_SIZE);
    // form string with chunk number and crc
    string chunkString = to_string(chunk) + " " + to_string(crc) + "\n";
    fileChunks.push_back(chunkString);
    chunk++;
  }
  ofstream torrentFileStream;
  torrentFileStream.open(torrentFile, ios_base::app);
  torrentFileStream << chunk << endl;
  for (unsigned int i = 0; i < fileChunks.size(); i++) {
    torrentFileStream << fileChunks[i];
  }
}

int main(int argc, char* argv[]) 
{	
  // TRACKER
  // ./tracker <peers-list> <input-file> <torrent-file> <log> 
  args trackerArgs = retrieveArgs(argv);

  readPeerListToTorrentFile(trackerArgs.peerList, trackerArgs.torrentFile);
  readInputFileToTorrentFile(trackerArgs.inputFile, trackerArgs.torrentFile);

  // distributes torrent files to any peer that connects
  // create thread for each peer
  // each thread will open a socket and send the torrent file


  return 0;
}
