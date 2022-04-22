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
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>

#include "PacketHeader.h"
#include "crc32.h"

#define FILE_CHUNK_SIZE 512000
#define PORT 6969
#define TORRENT_REQUEST 0
#define TORRENT_RESPONSE 1
#define HEADER_SIZE sizeof(PacketHeader)

std::mutex loggingMutex;

using namespace std; 

struct args {
  char* peerList;
  char* inputFile;
  char* torrentFile;
  char* log;
};

struct trackerSocketInfo {
  int sockfd;
  struct sockaddr_in server_addr;
  int peerSocketfd;
};

struct packet : public PacketHeader {
  char data[FILE_CHUNK_SIZE];
};

auto retrieveArgs(char* argv[]) {
  args newArgs;
  newArgs.peerList = argv[1];
  newArgs.inputFile = argv[2];
  newArgs.torrentFile = argv[3];
  newArgs.log = argv[4];
  return newArgs;
}

void writeToLogFile(char* logFile, char* iPAddress, char* packetType, char* packetLength) {
  ofstream logFileStream;
  logFileStream.open(logFile, ios::app);
  logFileStream << iPAddress << " " << packetType << " " << packetLength << endl;
  logFileStream.close();
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
    unsigned int crc = crc32(buffer, inputFileStream.gcount());
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
  if (bind(trackerSocket.sockfd, (sockaddr*)&trackerSocket.server_addr, sizeof(trackerSocket.server_addr)) == -1) {
    cout << "Error binding tracker socket" << endl;
    exit(0);
  }
  // Listen for incoming connections
  if (listen(trackerSocket.sockfd, 10) == -1) {
    cout << "Error listening for incoming connections" << endl;
    exit(0);
  }
  return trackerSocket;
}

void sendTorrentFile(int sockfd, char* torrentFile) {
  packet packetToSend;
  memset(packetToSend.data, 0, sizeof(packetToSend.data));
  packetToSend.type = TORRENT_RESPONSE;
  // Open torrent file
  ifstream torrentFileStream(torrentFile, ios::binary);
  // Read torrent file into packet
  torrentFileStream.read(packetToSend.data, FILE_CHUNK_SIZE);
  unsigned int bytesRead = torrentFileStream.gcount();
  packetToSend.length = bytesRead;

  // Send torrent file
  ssize_t bytesSent = send(sockfd, &packetToSend, HEADER_SIZE + bytesRead, MSG_NOSIGNAL);
  if (bytesSent == -1) {
    cout << "Error sending torrent file" << endl;
    exit(0);
  }
  torrentFileStream.close();
  cout << "Sent torrent file" << endl;
}

void receiveDataAndRespond(trackerSocketInfo &trackerSocketInfo, args trackerArgs) {
  // create packet to receive into
  PacketHeader p;
  // receive data
  int n = recv(trackerSocketInfo.peerSocketfd, &p, sizeof(p), MSG_WAITALL);
  if (n == -1) {
    cout << "Error receiving data" << endl;
    exit(0);
  }
  
  if (p.type == TORRENT_REQUEST) {
    cout << "Received torrent request packet" << endl;
    // write to log file
    // writeToLogFile(trackerArgs.log, inet_ntoa(p.client_addr.sin_addr), "0", to_string(p.length));
    // https://stackoverflow.com/questions/1276294/getting-ipv4-address-from-a-sockaddr-structure 
    
    char *ip = inet_ntoa(trackerSocketInfo.server_addr.sin_addr);
    cout << "SENDING TO IP: " << ip << endl;
    // send torrent file
    sendTorrentFile(trackerSocketInfo.peerSocketfd, trackerArgs.torrentFile);
  }
}

void acceptPeerConnection(trackerSocketInfo &trackerSocketInfo, args &trackerArgs) {
  // Accept incoming connection
  socklen_t addr_len = sizeof(trackerSocketInfo.server_addr);
  cout << "Trying to accept incoming connection" << endl;
  trackerSocketInfo.peerSocketfd = accept(trackerSocketInfo.sockfd, (sockaddr*)&trackerSocketInfo.server_addr, &addr_len);
  if (trackerSocketInfo.peerSocketfd == -1) {
    cout << "Error accepting incoming connection" << endl;
    exit(0);
  }
  cout << "I connected to a peer" << endl;
  receiveDataAndRespond(trackerSocketInfo, trackerArgs);
}

int main(int argc, char* argv[]) {
  // TRACKER
  // TODO - determine if peers-list is path or file
  // ./tracker <peers-list> <input-file> <torrent-file> <log> 
  args trackerArgs = retrieveArgs(argv);

  // Torrent file creation
  vector<string> peerList;
  readPeerListToTorrentFile(trackerArgs.peerList, trackerArgs.torrentFile, peerList);
  readInputFileToTorrentFile(trackerArgs.inputFile, trackerArgs.torrentFile);

  trackerSocketInfo trackerSocket = setupTrackerToListen();
  
  vector<thread> threads;
  unsigned int peersConnected = 0;
  while (peersConnected < peerList.size()) {
    threads.push_back(thread(&acceptPeerConnection, ref(trackerSocket), ref(trackerArgs)));
    peersConnected++;
  }

  // Join threads
  for (unsigned int i = 0; i < threads.size(); i++) {
    threads[i].join();
  }
  
  close(trackerSocket.sockfd);
  return 0;
}
