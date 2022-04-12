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
#define FILE_CHUNK_REQUEST 2

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
  vector<peerSocketInfo> peerSockets; // client peer sockets
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

// ALL PEERS DO THIS 
peerServerInfo setupServerPeerSocket() {
  peerServerInfo peerServerSocket;
  // create socket
  peerServerSocket.sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (peerServerSocket.sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }
  // Allow port number to be reused
  int optval = 1;
  if (setsockopt(peerServerSocket.sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    perror("ERROR on setsockopt");
    exit(1);
  }
  // Bind socket to PORT 6881
  peerServerSocket.server_len = sizeof(peerServerSocket.server_addr);
  peerServerSocket.server_addr.sin_family = AF_INET;
  peerServerSocket.server_addr.sin_addr.s_addr = INADDR_ANY;
  peerServerSocket.server_addr.sin_port = htons((u_short) PEERPORT);
  if (bind(peerServerSocket.sockfd, (struct sockaddr *) &peerServerSocket.server_addr, sizeof(peerServerSocket.server_addr)) < 0) {
    perror("ERROR on binding");
    exit(1);
  }
  // listen for connections
  if (listen(peerServerSocket.sockfd, 5) < 0) {
    perror("ERROR on listening");
    exit(1);
  }
  cout << "SERVER SOCKET IS SETUP" << endl;
  return peerServerSocket;
}

void handlePeerRequest() {
  cout << "HANDLING PEER REQUEST" << endl;
}

void acceptPeerConnection(peerServerInfo &peerServerSocket, vector<string> &peerList) {
  // Accept peerList - 1 connections
  int numberOfPeersToAccept = peerList.size() - 1;
  while (numberOfPeersToAccept != 0) {
    socklen_t addr_len = sizeof(peerServerSocket.sockfd);
    cout << "SERVER: Trying to accept incoming connection. There are " << numberOfPeersToAccept << " peers left to accept" << endl;
    peerServerSocket.peerSocketfd = accept(peerServerSocket.sockfd, (struct sockaddr*)&peerServerSocket.server_addr, &addr_len);
    if (peerServerSocket.peerSocketfd == -1) {
      cout << "Error accepting incoming connection" << endl;
      exit(0);
    }
    cout << "SERVER: a client accepted my connection. Creating new thread to handle connection..." << endl;
    numberOfPeersToAccept--;

    // spawn new thread to handle request
    thread t1(handlePeerRequest);
    t1.join(); // detach?

    // immediately go back to accepting connections
  }
  cout << "SERVER: All peers accepted!" << endl;
}

peerSocketInfo connectToServerPeer(char* myIP, const char* peerIP) {
  peerSocketInfo peerSocket;
  // create socket
  peerSocket.sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (peerSocket.sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }
  // connect to the peers's binded socket
  peerSocket.server_addr.sin_family = AF_INET;
  peerSocket.server_addr.sin_addr.s_addr = INADDR_ANY;
  peerSocket.server_addr.sin_port = htons((u_short) PEERPORT);
  struct hostent *tracker_host = gethostbyname(peerIP);
  if (tracker_host == NULL) {
    perror("ERROR, no such host");
    exit(1);
  }
  memcpy(&peerSocket.server_addr.sin_addr, tracker_host->h_addr_list[0], tracker_host->h_length);
  cout << "CLIENT: Attempting to connect to peer: " << peerIP << endl;
  while (connect(peerSocket.sockfd, (struct sockaddr *) &peerSocket.server_addr, sizeof(peerSocket.server_addr)) < 0) {
    continue;
  }
  cout << "CLIENT: Server peer " << peerIP << " accepted my connection" << endl;
  return peerSocket;
}

// void requestFileChunksFromPeer(peerSocketInfo &peerSocket) {
//   packet fileChunkRequest;
//   fileChunkRequest.type = FILE_CHUNK_REQUEST;
//   fileChunkRequest.length = 0;
//   send(peerSocket.sockfd, &fileChunkRequest, sizeof(fileChunkRequest), MSG_NOSIGNAL);
//   cout << "Requested file chunks!" << endl;
// }

void connectToEachPeer(char* myIP, torrentData &torrentData) {
  for (unsigned int i = 0; i < torrentData.peerList.size(); i++) {
    if (myIP != torrentData.peerList[i]) {
      peerSocketInfo peerSocket = connectToServerPeer(myIP, torrentData.peerList[i].c_str());
      // save peer socket
      torrentData.peerSockets.push_back(peerSocket);
    }
  }
  cout << "CLIENT: Finished connecting to all peers" << endl;
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


  // All outgoing requests can be done sequentially
  // - requests for asking for chunks peer owns
  // - request for actually getting the chunk

  // Must be able to send out requests to other peers while also accepting requests from other peers simultaneously

  // All requests to other peers can occur in one thread (sequential in seperate thread?)
  // Other threads will be accepting requests from other peers at the same time.
  // - When accepting requests, each request should be handled simultaneously.
  // - 1. Accept request. 2. Spawn new thread to handle request 3. Start waiting to accept again

  // setup peer server socket 
  peerServerInfo peerServerSocket = setupServerPeerSocket();
  // 1. Accept request, 2. Spawn new thread to handle request 3. Start waiting to accept again
  thread acceptingPeers(acceptPeerConnection, ref(peerServerSocket), ref(torrentData.peerList));
  // sequentially connect to each peer
  connectToEachPeer(peerArgs.myIP, torrentData);
  acceptingPeers.join();

  // close socket
  // close(peerSocket.sockfd);
  return 0;
}
