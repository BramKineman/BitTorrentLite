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
#include <map>
#include <algorithm>

#include "PacketHeader.h"
#include "crc32.h"

#define TRACKERPORT 6969
#define FILE_CHUNK_SIZE 512000
#define TORRENT_REQUEST 0

using namespace std; 

struct args {
  char* myIP;
  char* trackerIP;
  char* inputFile;
  char* ownedChunks;
  char* outputFile;
  char* log;
};

struct peerSocketInfo {
  int sockfd;
  struct sockaddr_in tracker_addr;
  socklen_t server_len;
};

struct packet : public PacketHeader {
  char data[FILE_CHUNK_SIZE];
};

struct torrentData {
  vector<string> peerList;
  map<int, string> chunkList;
  vector<int> ownedChunks;
  vector<int> neededChunks;
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

peerSocketInfo connectToTracker(char* myIP, char* trackerIP) {
  peerSocketInfo peerSocket;
  // create socket
  peerSocket.sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (peerSocket.sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }
  // connect to the tracker's binded socket
  peerSocket.tracker_addr.sin_family = AF_INET;
  peerSocket.tracker_addr.sin_addr.s_addr = INADDR_ANY;
  peerSocket.tracker_addr.sin_port = htons((u_short) TRACKERPORT);
  struct hostent *tracker_host = gethostbyname(trackerIP);
  if (tracker_host == NULL) {
    perror("ERROR, no such host");
    exit(1);
  }
  memcpy(&peerSocket.tracker_addr.sin_addr, tracker_host->h_addr_list[0], tracker_host->h_length);
  if (connect(peerSocket.sockfd, (struct sockaddr *) &peerSocket.tracker_addr, sizeof(peerSocket.tracker_addr)) < 0) {
    perror("ERROR connecting");
    exit(1);
  }
  cout << "Connected to tracker" << endl;

  return peerSocket;
}

packet createTorrentRequestPacket() {
  packet torrentRequest;
  torrentRequest.type = TORRENT_REQUEST;
  torrentRequest.length = 0;
  return torrentRequest;
}

void requestTorrentFileFromTracker(peerSocketInfo &peerSocket) {
  packet torrentRequest = createTorrentRequestPacket();
  cout << "Sending data with type " << torrentRequest.type << endl;
  send(peerSocket.sockfd, &torrentRequest, sizeof(torrentRequest), MSG_NOSIGNAL);
  cout << "Sent!" << endl;
}

packet receiveTorrentFileFromTracker(peerSocketInfo &peerSocket) {
  packet torrentFile;
  // clear torrentFile.data buffer
  memset(&torrentFile.data, 0, sizeof(torrentFile.data));
  int bytesReceived = recv(peerSocket.sockfd, &torrentFile, sizeof(torrentFile), 0);
  if (bytesReceived < 0) {
    perror("ERROR receiving data");
    exit(1);
  }
  cout << "Received torrent file" << endl;
  cout << "With data: " << torrentFile.data << endl;
  return torrentFile;
}

torrentData parseTorrentFile(char* torrentFile) {
  torrentData torrentData;
  char* numPeers = strtok(torrentFile, "\n");
  
  int numPeersInt = atoi(numPeers);
  for (int i = 0; i < numPeersInt; i++) {
    char* peer = strtok(NULL, "\n");
    torrentData.peerList.push_back(peer);
  }
  char* numChunks = strtok(NULL, "\n");
  int numChunksInt = atoi(numChunks);
  for (int i = 0; i < numChunksInt; i++) {
    strtok(NULL, " ");
    char* chunkHash = strtok(NULL, "\n");
    torrentData.chunkList[i] = chunkHash;
  }
  return torrentData;
}

void getOwnedChunksFromFile(char* ownedChunks, torrentData &torrentData) {
  ifstream file(ownedChunks);
  string line;
  while (getline(file, line)) {
    torrentData.ownedChunks.push_back(stoi(line));
  }
}

void determineNeededChunks(torrentData &torrentData) {
  for (unsigned int i = 0; i < torrentData.chunkList.size(); i++) {
    if (find(torrentData.ownedChunks.begin(), torrentData.ownedChunks.end(), i) == torrentData.ownedChunks.end()) {
      torrentData.neededChunks.push_back(i);
    }
  }
}

int main(int argc, char* argv[]) 
{	
  // PEER
  // ./peer <my-ip> <tracker-ip> <input-file> <owned-chunks> <output-file> <log> 
  args peerArgs = retrieveArgs(argv);

  peerSocketInfo peerSocket = connectToTracker(peerArgs.myIP, peerArgs.trackerIP);

  requestTorrentFileFromTracker(peerSocket);
  packet torrentFilePacket = receiveTorrentFileFromTracker(peerSocket);
  torrentData torrentData = parseTorrentFile(torrentFilePacket.data);

  // determine which chunks to request
  getOwnedChunksFromFile(peerArgs.ownedChunks, torrentData);
  determineNeededChunks(torrentData);

  // loop through torrentData.peerlist
  for (unsigned int i = 0; i < torrentData.peerList.size(); i++) {
    cout << "Connecting to peer " << torrentData.peerList[i] << endl;
  }
  
  // close socket
  close(peerSocket.sockfd);
  return 0;
}
