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
#include <list>
#include <utility>

#include "PacketHeader.h"
#include "crc32.h"

#define TRACKERPORT 6969
#define PEERPORT 6881
#define FILE_CHUNK_SIZE 512000
#define TORRENT_REQUEST 0
#define TORRENT_RESPONSE 1
#define CHUNK_LIST_REQUEST 2
#define CHUNK_LIST_RESPONSE 3
#define CHUNK_REQUEST 4
#define UNSIGNED_INT_SIZE 4
#define HEADER_SIZE sizeof(PacketHeader)

using namespace std; 

struct args {
  char* myIP;
  char* trackerIP;
  char* inputFile;
  char* ownedChunks;
  char* outputFile;
  char* log;
};

struct peerSocketInfo { // client peer info
  int sockfd;
  struct sockaddr_in server_addr;
  socklen_t server_len;
};

struct peerServerInfo { // server peer info
  int sockfd;
  struct sockaddr_in server_addr;
  int peerConnectionfd; // returned value from accept - recv from this socket
  socklen_t server_len;

};

struct packet : public PacketHeader {
  char data[FILE_CHUNK_SIZE];
};

struct torrentData {
  vector<string> peerList;
  vector<string> otherPeers;
  map<int, string> chunkList;
  char* ownedChunksFile;
  vector<unsigned int> ownedChunks;
  vector<unsigned int> neededChunks;
  vector<peerSocketInfo> peerClientSockets; // sockets for SENDING
  vector<vector<unsigned int>> peersOwnedChunks; // list of chunks owned by each peer, index corresponds to peerClientSockets index
  vector<pair<peerSocketInfo, vector<unsigned int>>> serverPeerOwnedChunks;
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

void getOwnedChunksFromFile(torrentData &torrentData) {
  ifstream file(torrentData.ownedChunksFile);
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

peerServerInfo setupServerPeerSocketToListen() {
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
  if (listen(peerServerSocket.sockfd, 5) < 0) { // we allow 5 simultaneous connections in backlog
    perror("ERROR on listening");
    exit(1);
  }
  cout << "Opened peer server socket..." << endl;
  return peerServerSocket;
}

void peerServerSendChunkListResponse(peerServerInfo &peerServerSocket, torrentData &torrentData) {
  cout << "SERVER: Received chunk list request from peer" << endl;
  packet packetToSend;
  memset(&packetToSend.data, 0, sizeof(packetToSend.data));
  packetToSend.type = CHUNK_LIST_RESPONSE;
  // memcpy each chunk number into packetToSend.data
  for (unsigned int i = 0; i < torrentData.ownedChunks.size(); i++) {
    memcpy(&packetToSend.data[i * UNSIGNED_INT_SIZE], (char*)&torrentData.ownedChunks[i], UNSIGNED_INT_SIZE);
  }
  packetToSend.length = (UNSIGNED_INT_SIZE * (torrentData.ownedChunks.size()));
  // Send packet
  ssize_t bytesSent = send(peerServerSocket.peerConnectionfd, &packetToSend, packetToSend.length + HEADER_SIZE, MSG_NOSIGNAL);
  if (bytesSent == -1) {
    cout << "SERVER: Error sending chunk list file with error: " << strerror(errno) << endl;
    exit(1);
  }
  cout << "SERVER: Sent chunk list file to peer" << endl;
}

void peerServerReceiveAndSendData(peerServerInfo &peerServerSocket, torrentData &torrentData) {
  packet packetToReceive;
  // memset(&packetToReceive, 0, sizeof(packetToReceive));
  cout << "SERVER: Attempting to receive data from peer..." << endl;
  // receive packet
  recv(peerServerSocket.peerConnectionfd, &packetToReceive, sizeof(packetToReceive), 0); // receive, then send correct thing 
  if (packetToReceive.type == CHUNK_LIST_REQUEST) {
    peerServerSendChunkListResponse(peerServerSocket, torrentData);
  }
  if (packetToReceive.type == CHUNK_REQUEST) {
    cout << "Received chunk request from peer" << endl;
  }
}

void serverAcceptClientPeerConnection(peerServerInfo &peerServerSocket, torrentData &torrentData) {
  while (true) { // infinite loop
    socklen_t addr_len = sizeof(peerServerSocket.sockfd);
    cout << "SERVER: Trying to accept incoming connection...  " << endl;
    peerServerSocket.peerConnectionfd = accept(peerServerSocket.sockfd, (struct sockaddr*)&peerServerSocket.server_addr, &addr_len);
    if (peerServerSocket.peerConnectionfd == -1) {
      cout << "Error accepting incoming connection" << endl;
      exit(0);
    }
    cout << "SERVER: a client connected to me. Creating new thread to receive data..." << endl;

    // spawn new thread to handle request
    thread t1(peerServerReceiveAndSendData, ref(peerServerSocket), ref(torrentData)); // I can recv(peerServerSocket.peerConnectionfd)
    t1.join(); // TODO: Join elsewhere?
    // immediately go back to accepting connections
  }
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

void clientReceiveFileChunkListFromServerPeer(peerSocketInfo &peerSocket, torrentData &torrentData) {
  packet fileChunkResponse;
  vector<unsigned int> fileChunkList;
  recv(peerSocket.sockfd, &fileChunkResponse, sizeof(fileChunkResponse), 0);
  if (fileChunkResponse.type == CHUNK_LIST_RESPONSE) {
    cout << "CLIENT: Received chunk list response from server peer" << endl;
    for (unsigned int i = 0; i <= (fileChunkResponse.length-1); i += UNSIGNED_INT_SIZE) { 
      unsigned int chunkNumber;
      memcpy(&chunkNumber, &fileChunkResponse.data[i], UNSIGNED_INT_SIZE);
      fileChunkList.push_back(chunkNumber);
    }
  }
  pair<peerSocketInfo, vector<unsigned int>> newPair = make_pair(peerSocket, fileChunkList);
  torrentData.serverPeerOwnedChunks.push_back(newPair);
  shutdown(peerSocket.sockfd, SHUT_RDWR);
}

void clientRequestFileChunkListFromServerPeer(peerSocketInfo &peerSocket, torrentData &torrentData, const char* serverIP) {
  PacketHeader fileChunkRequest;
  fileChunkRequest.type = CHUNK_LIST_REQUEST;
  fileChunkRequest.length = 0;
  cout << "CLIENT: Requesting file chunk list from peer..." << endl;
  send(peerSocket.sockfd, &fileChunkRequest, sizeof(fileChunkRequest), MSG_NOSIGNAL);
  cout << "CLIENT: Requested file chunk list from peer..." << endl;
  clientReceiveFileChunkListFromServerPeer(peerSocket, torrentData);
}

void connectToEachServerPeerAndRequest(char* myIP, torrentData &torrentData) {
  for (unsigned int i = 0; i < torrentData.peerList.size(); i++) {
    if (myIP != torrentData.peerList[i]) {
      peerSocketInfo clientSocketInfo = connectToServerPeer(myIP, torrentData.peerList[i].c_str());
      torrentData.peerClientSockets.push_back(clientSocketInfo);
      clientRequestFileChunkListFromServerPeer(clientSocketInfo, torrentData, torrentData.peerList[i].c_str());
    }
  }
  cout << "*** CLIENT: Finished connecting to all peers ***" << endl;
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
  torrentData.ownedChunksFile = peerArgs.ownedChunks;

  // determine which chunks to request
  getOwnedChunksFromFile(torrentData);
  determineNeededChunks(torrentData);

  // All outgoing requests can be done sequentially
  // - requests for asking for chunks peer owns
  // - request for actually getting the chunk

  // setup peer server socket 
  peerServerInfo peerServerSocket = setupServerPeerSocketToListen();
  // 1. Accept request, 2. Spawn new thread to handle request 3. Start waiting to accept again
  thread acceptingPeers(serverAcceptClientPeerConnection, ref(peerServerSocket), ref(torrentData));

  // sequentially connect to each peer
  connectToEachServerPeerAndRequest(peerArgs.myIP, torrentData);

  // output torrent data server peers owned chunks


  // KEEP PEER RUNNING
  acceptingPeers.join();
  return 0;
}
