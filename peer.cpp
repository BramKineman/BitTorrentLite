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
#include <thread>
#include <pthread.h>
#include <mutex>

#include "PacketHeader.h"
#include "crc32.h"

#define TRACKERPORT 6969
#define PEERPORT 6881
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
  struct sockaddr_in server_addr;
  socklen_t server_len;
};

struct peerServerInfo {
  int sockfd;
  struct sockaddr_in server_addr;
  int peerSocketfd;
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
  peerSocket.server_addr.sin_family = AF_INET;
  peerSocket.server_addr.sin_addr.s_addr = INADDR_ANY;
  peerSocket.server_addr.sin_port = htons((u_short) TRACKERPORT);
  struct hostent *tracker_host = gethostbyname(trackerIP);
  if (tracker_host == NULL) {
    perror("ERROR, no such host");
    exit(1);
  }
  memcpy(&peerSocket.server_addr.sin_addr, tracker_host->h_addr_list[0], tracker_host->h_length);
  if (connect(peerSocket.sockfd, (struct sockaddr *) &peerSocket.server_addr, sizeof(peerSocket.server_addr)) < 0) {
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
  cout << "Requesting torrent file with type " << torrentRequest.type << endl;
  send(peerSocket.sockfd, &torrentRequest, sizeof(torrentRequest), MSG_NOSIGNAL);
  cout << "Requested!" << endl;
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
  cout << "With data: " << endl << torrentFile.data << endl;
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

peerServerInfo setupPeerToListen() {
  peerServerInfo peerToPeerSocket;
  // create socket
  peerToPeerSocket.sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (peerToPeerSocket.sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }
  // Allow port number to be reused
  int optval = 1;
  if (setsockopt(peerToPeerSocket.sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    perror("ERROR on setsockopt");
    exit(1);
  }
  // Bind socket to PORT 6881
  peerToPeerSocket.server_len = sizeof(peerToPeerSocket.server_addr);
  peerToPeerSocket.server_addr.sin_family = AF_INET;
  peerToPeerSocket.server_addr.sin_addr.s_addr = INADDR_ANY;
  peerToPeerSocket.server_addr.sin_port = htons((u_short) PEERPORT);
  if (bind(peerToPeerSocket.sockfd, (struct sockaddr *) &peerToPeerSocket.server_addr, sizeof(peerToPeerSocket.server_addr)) < 0) {
    perror("ERROR on binding");
    exit(1);
  }
  // listen for connections
  if (listen(peerToPeerSocket.sockfd, 5) < 0) {
    perror("ERROR on listening");
    exit(1);
  }
  cout << "Listening for connections" << endl;
  return peerToPeerSocket;
}

void acceptPeerConnection(peerServerInfo &peerToPeerSocket) {
  // Accept incoming connection
  socklen_t addr_len = sizeof(peerToPeerSocket.sockfd);
  cout << "Trying to accept incoming connection" << endl;
  peerToPeerSocket.peerSocketfd = accept(peerToPeerSocket.sockfd, (struct sockaddr*)&peerToPeerSocket.server_addr, &addr_len);
  if (peerToPeerSocket.peerSocketfd == -1) {
    cout << "Error accepting incoming connection" << endl;
    exit(0);
  }
  cout << "I connected to a peer" << endl;
}

// void requestPeerListFromPeer(peerSocketInfo &peerSocket) {
//   packet peerListRequest;
//   peerListRequest.type = PEER_LIST_REQUEST;
//   peerListRequest.length = 0;
//   send(peerSocket.sockfd, &peerListRequest, sizeof(peerListRequest), MSG_NOSIGNAL);
//   cout << "Requested peer list!" << endl;
// }

peerSocketInfo connectToPeer(char* myIP, const char* peerIP) {
  peerSocketInfo peerSocket;
  // create socket
  peerSocket.sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (peerSocket.sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }
  // connect to the tracker's binded socket
  peerSocket.server_addr.sin_family = AF_INET;
  peerSocket.server_addr.sin_addr.s_addr = INADDR_ANY;
  peerSocket.server_addr.sin_port = htons((u_short) PEERPORT);
  struct hostent *tracker_host = gethostbyname(peerIP);
  if (tracker_host == NULL) {
    perror("ERROR, no such host");
    exit(1);
  }
  memcpy(&peerSocket.server_addr.sin_addr, tracker_host->h_addr_list[0], tracker_host->h_length);
  if (connect(peerSocket.sockfd, (struct sockaddr *) &peerSocket.server_addr, sizeof(peerSocket.server_addr)) < 0) {
    perror("ERROR connecting");
    exit(1);
  }
  cout << "Connected to tracker" << endl;

  return peerSocket;
}

void connectToEachPeer(char* myIP, torrentData &torrentData) {
  for (unsigned int i = 0; i < torrentData.peerList.size(); i++) {
    connectToPeer(myIP, torrentData.peerList[i].c_str());
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
  // orderly shutdown of the socket
  shutdown(peerSocket.sockfd, SHUT_RDWR);

  torrentData torrentData = parseTorrentFile(torrentFilePacket.data);

  // determine which chunks to request
  getOwnedChunksFromFile(peerArgs.ownedChunks, torrentData);
  determineNeededChunks(torrentData);

  // connect to each peer 
  // (ALL ON SAME (BUT NEW?) THREAD))
  peerServerInfo peerToPeerSocket = setupPeerToListen();
  thread connectToPeersThread(&connectToEachPeer, ref(peerArgs.myIP), ref(torrentData));
  connectToPeersThread.join();
  
  // ask each peer for its owned chunks <-- Sequential
  // receive the owned chunks from a peer <-- Threaded

  // All outgoing requests can be done sequentially
  // - requests for asking for chunks
  // - request for actually getting the chunk

  
  // close socket
  close(peerSocket.sockfd);
  return 0;
}
