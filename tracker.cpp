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
#include <thread>
#include <pthread.h>
#include <mutex>

#include "PacketHeader.h"
#include "crc32.h"

#define FILE_CHUNK_SIZE 512000
#define PORT 6969

using namespace std; 

struct args {
  char* peerList;
  char* inputFile;
  char* torrentFile;
  char* log;
};

// tracket socket info for port binding
struct trackerSocketInfo {
  int sockfd;
  struct sockaddr_in server_addr;
  int peerSocketfd;
};

auto retrieveArgs(char* argv[]) {
  args newArgs;
  newArgs.peerList = argv[1];
  newArgs.inputFile = argv[2];
  newArgs.torrentFile = argv[3];
  newArgs.log = argv[4];
  return newArgs;
}

void readPeerListToTorrentFile(char* &peerListPath, char* &torrentFile, vector<string> &peerList) {
  ifstream peerListFile(peerListPath);
  string line;
  ofstream torrentFileStream;
  torrentFileStream.open(torrentFile);
  while (getline(peerListFile, line)) {
    peerList.push_back(line);
  }
  torrentFileStream << peerList.size() << endl;
  for (unsigned int i = 0; i < peerList.size(); i++) {
    torrentFileStream << peerList[i] << endl;
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

trackerSocketInfo setupTrackerToListen() {
  trackerSocketInfo trackerSocket;
  // create tracker socket
  trackerSocket.sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (trackerSocket.sockfd == -1) {
    cout << "Error creating tracker socket" << endl;
    exit(0);
  }
  // Allow port number to be reused
  int optval = 1;
  if (setsockopt(trackerSocket.sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
    cout << "Error setting socket options" << endl;
    exit(0);
  }
  // Bind tracker socket to PORT 6969
  memset(&trackerSocket.server_addr, 0, sizeof(trackerSocket.server_addr));
  trackerSocket.server_addr.sin_family = AF_INET;
  trackerSocket.server_addr.sin_addr.s_addr = INADDR_ANY;
  trackerSocket.server_addr.sin_port = htons(PORT);
  if (bind(trackerSocket.sockfd, (struct sockaddr*)&trackerSocket.server_addr, sizeof(trackerSocket.server_addr)) == -1) {
    cout << "Error binding tracker socket" << endl;
    exit(0);
  }
  // Listen for incoming connections
  if (listen(trackerSocket.sockfd, 10) == -1) {
    cout << "Error listening for incoming connections" << endl;
    exit(0);
  }
  // Accept incoming connection
  socklen_t addr_len = sizeof(trackerSocket.sockfd);
  trackerSocket.peerSocketfd = accept(trackerSocket.sockfd, (struct sockaddr*)&trackerSocket.server_addr, &addr_len);
  if (trackerSocket.peerSocketfd == -1) {
    cout << "Error accepting incoming connection" << endl;
    exit(0);
  }
  return trackerSocket;
}

int main(int argc, char* argv[]) {
  // TRACKER
  // ./tracker <peers-list> <input-file> <torrent-file> <log> 
  args trackerArgs = retrieveArgs(argv);
  vector<string> peerList;
  readPeerListToTorrentFile(trackerArgs.peerList, trackerArgs.torrentFile, peerList);
  readInputFileToTorrentFile(trackerArgs.inputFile, trackerArgs.torrentFile);
  // distributes torrent files to any peer that connects
  // create thread for each peer
  // each thread will open a socket and send the torrent file

  // wait for connection request from peer and accept connection
  // while(true) {
  //   trackerSocketInfo trackerSocket = connectPeerSocket();
  //   // thread to handle request, send torrent file

  // }
  trackerSocketInfo trackerSocket = setupTrackerToListen();

  return 0;
}
