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
#include <tgmath.h>

#include "mutex.cpp"
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
#define CHUNK_RESPONSE 5
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

struct data {
  char data[FILE_CHUNK_SIZE];
};

struct torrentData {
  vector<string> peerList; // list of peer IPs
  vector<string> otherPeers; // list of peer IPs that are not the clients
  map<int, unsigned int> chunkList; // chunk number, chunk CRC - from the torrent file
  char* ownedChunksFile;
  char* inputFile;
  char* outputFile;
  vector<unsigned int> ownedChunks;
  vector<unsigned int> neededChunks;
  vector<peerSocketInfo> peerClientSockets; // sockets for SENDING
  vector<pair<const char *, vector<unsigned int>>> serverPeerOwnedChunks; // list [ pairs [IP, vector of chunks] ]
  map<unsigned int, unsigned int> nonOwnedChunksAndOccurrences; // <chunk, occurences> for non-owned chunks
  map<unsigned int, data> finalChunkData; // <chunk, data> for newly-owned chunks, preloaded with owned chunks
  map<unsigned int, unsigned int> chunkAndSize; // <chunk, size> for chunks
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

PacketHeader createTorrentRequestPacket() {
  PacketHeader torrentRequest;
  torrentRequest.type = TORRENT_REQUEST;
  torrentRequest.length = 0;
  return torrentRequest;
}

void requestTorrentFileFromTracker(peerSocketInfo &peerSocket, args peerArgs) {
  PacketHeader torrentRequest = createTorrentRequestPacket();
  cout << "Requesting torrent file with type " << torrentRequest.type << endl;
  send(peerSocket.sockfd, &torrentRequest, sizeof(torrentRequest), MSG_NOSIGNAL);
  // lock and log
  loggingMutex.lock();
  writeToLogFile(peerArgs.log, peerArgs.trackerIP, torrentRequest.type, torrentRequest.length);
  loggingMutex.unlock();
  cout << "Requested!" << endl;
}

packet receiveTorrentFileFromTracker(peerSocketInfo &peerSocket, args peerArgs) {
  packet torrentFile;
  // clear torrentFile.data buffer
  memset(&torrentFile.data, 0, sizeof(torrentFile.data));
  cout << "Receiving torrent file from tracker..." << endl;
  int bytesReceived = recv(peerSocket.sockfd, &torrentFile, HEADER_SIZE, MSG_WAITALL);
  if (bytesReceived < 0) {
    perror("ERROR receiving data");
    exit(1);
  }
  bytesReceived = recv(peerSocket.sockfd, &torrentFile.data, torrentFile.length, MSG_WAITALL);
  if (bytesReceived < 0) {
    perror("ERROR receiving data");
    exit(1);
  }
  // lock and log
  loggingMutex.lock();
  writeToLogFile(peerArgs.log, peerArgs.trackerIP, torrentFile.type, torrentFile.length);
  loggingMutex.unlock();

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
    // convert chunkHash to unsigned int
    unsigned int chunkHashInt = 0;
    for (unsigned int i = 0; i < strlen(chunkHash); i++) {
      chunkHashInt += (chunkHash[i] - '0') * pow(10, strlen(chunkHash) - i - 1);
    }
    torrentData.chunkList[i] = chunkHashInt;
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

void readOwnedChunksIntoTorrentData(torrentData &torrentData) {
  ifstream file(torrentData.inputFile);
  // for each chunk number in torrentData.ownedChunks
  for (unsigned int i = 0; i < torrentData.ownedChunks.size(); i++) {
    // read chunk data into torrentData.finalChunkData
    char chunkData[FILE_CHUNK_SIZE];
    file.seekg(torrentData.ownedChunks[i] * FILE_CHUNK_SIZE);
    file.read(chunkData, FILE_CHUNK_SIZE);
    data chunkDataStruct;
    memcpy(chunkDataStruct.data, chunkData, file.gcount()); // only copying over the bytes that were read
    torrentData.finalChunkData.insert(pair<unsigned int, data>(torrentData.ownedChunks[i], chunkDataStruct));
    torrentData.chunkAndSize.insert(pair<unsigned int, unsigned int>(torrentData.ownedChunks[i], file.gcount()));
  }
}

void determineNeededChunks(torrentData &torrentData) {
  for (unsigned int i = 0; i < torrentData.chunkList.size(); i++) {
    if (find(torrentData.ownedChunks.begin(), torrentData.ownedChunks.end(), i) == torrentData.ownedChunks.end()) {
      torrentData.neededChunks.push_back(i);
    }
  }
}

void generateOtherPeerList(torrentData &torrentData, char* myIP) {
  for (unsigned int i = 0; i < torrentData.peerList.size(); i++) {
    if (torrentData.peerList[i] != myIP) {
      torrentData.otherPeers.push_back(torrentData.peerList[i]);
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

void peerServerSendChunkResponse(peerServerInfo &peerServerSocket, torrentData &torrentData, unsigned int requestedChunkCRC) {
  int chunkNum;
  bool chunkFound = false;
  // search for chunk in chunkList
  for (unsigned int i = 0; i < torrentData.chunkList.size(); i++) {
    if (torrentData.chunkList[i] == requestedChunkCRC) { // if CRC exists
      chunkFound = true;
      chunkNum = i;
      cout << "SERVER: Received chunk request for chunk " << chunkNum << " with CRC " << requestedChunkCRC << endl;

      packet packetToSend;
      packetToSend.type = CHUNK_RESPONSE;
      // can only read from the portion of file that is owned
      ifstream file(torrentData.inputFile, ios::binary);
      file.seekg(chunkNum * FILE_CHUNK_SIZE);
      cout << "SERVER: Starting to read at file position " << chunkNum * FILE_CHUNK_SIZE << " which should be " << file.tellg() << endl;
      file.read(packetToSend.data, FILE_CHUNK_SIZE); // read full chunk size or until EOF
      file.close();
      packetToSend.length = file.gcount(); // Should be amount of bytes read

      unsigned int chunkCRC = crc32(packetToSend.data, file.gcount());

      // output bytes read
      cout << "SERVER: Read " << file.gcount() << " bytes from file" << endl;  

      // compute CRC of chunk
      cout << "SERVER: I am sending chunk " << chunkNum << " that has a server computed CRC of " << chunkCRC << endl;
      cout << "SERVER: The length of the packet I am sending is " << packetToSend.length << " and the length of the total packet is " << packetToSend.length + HEADER_SIZE << endl;

      cout << "SERVER: Sending chunk to peer" << endl; 
      ssize_t bytesSent = send(peerServerSocket.peerConnectionfd, &packetToSend, packetToSend.length + HEADER_SIZE, MSG_NOSIGNAL);
      if (bytesSent == -1) {
        cout << "SERVER: Error sending chunk with error: " << strerror(errno) << endl;
        exit(1);
      }
      cout << "SERVER: Sent chunk to peer" << endl;
      break;
    }
  }
  if (!chunkFound) {
    cout << "SERVER: XXXXXXXXXXXX Didn't find a matching chunk for requested CRC: " << requestedChunkCRC << " XXXXXXXXXXXXXXXXXXX" << endl;
  }
}

void peerServerReceiveAndSendData(peerServerInfo &peerServerSocket, torrentData &torrentData, args peerArgs) {
  packet packetToReceive;
  // memset(&packetToReceive, 0, sizeof(packetToReceive));
  cout << "SERVER: Attempting to receive data from peer..." << endl;
  // receive packet
  recv(peerServerSocket.peerConnectionfd, &packetToReceive, HEADER_SIZE, MSG_WAITALL); // receive, then send correct thing 
  char *ip = inet_ntoa(peerServerSocket.server_addr.sin_addr); // IP data was received from
  if (packetToReceive.type == CHUNK_LIST_REQUEST) {
    // lock and log
    loggingMutex.lock();
    writeToLogFile(peerArgs.log, ip, packetToReceive.type, packetToReceive.length);
    loggingMutex.unlock();

    peerServerSendChunkListResponse(peerServerSocket, torrentData);
  }
  if (packetToReceive.type == CHUNK_REQUEST) {
    recv(peerServerSocket.peerConnectionfd, &packetToReceive.data, packetToReceive.length, MSG_WAITALL);
    // lock and log
    loggingMutex.lock();
    writeToLogFile(peerArgs.log, ip, packetToReceive.type, packetToReceive.length);
    loggingMutex.unlock();

    unsigned int chunkCRC = 0;
    memcpy(&chunkCRC, &packetToReceive.data[0], UNSIGNED_INT_SIZE);
    peerServerSendChunkResponse(peerServerSocket, torrentData, chunkCRC);
  }
}

void serverAcceptClientPeerConnection(peerServerInfo &peerServerSocket, torrentData &torrentData, vector<thread> &threads, args &peerArgs) {
  while (true) { // infinite loop
    socklen_t addr_len = sizeof(peerServerSocket.server_addr);
    cout << "SERVER: Trying to accept incoming connection...  " << endl;
    peerServerSocket.peerConnectionfd = accept(peerServerSocket.sockfd, (struct sockaddr*)&peerServerSocket.server_addr, &addr_len);
    if (peerServerSocket.peerConnectionfd == -1) {
      cout << "Error accepting incoming connection" << endl;
      exit(0);
    }
    cout << "SERVER: a client connected to me. Creating new thread to receive data..." << endl;
    // spawn new thread to handle request
    threads.push_back(thread(peerServerReceiveAndSendData, ref(peerServerSocket), ref(torrentData), ref(peerArgs)));
    // immediately go back to accepting connections
  }
  // join all threads
  for (unsigned int i = 0; i < threads.size(); i++) {
    threads[i].join();
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

void clientReceiveFileChunkListFromServerPeer(peerSocketInfo &peerSocket, torrentData &torrentData, const char *serverIP, args peerArgs) {
  packet fileChunkResponse;
  vector<unsigned int> fileChunkList;
  // recv(peerSocket.sockfd, &fileChunkResponse, sizeof(fileChunkResponse), 0);
  recv(peerSocket.sockfd, &fileChunkResponse, HEADER_SIZE, MSG_WAITALL);
  if (fileChunkResponse.type == CHUNK_LIST_RESPONSE) {
    recv(peerSocket.sockfd, &fileChunkResponse.data, fileChunkResponse.length, MSG_WAITALL);

    // lock and log
    loggingMutex.lock();
    writeToLogFile(peerArgs.log, serverIP, CHUNK_LIST_RESPONSE, fileChunkResponse.length);
    loggingMutex.unlock();

    cout << "CLIENT: Received chunk list response from server peer" << endl;
    // put the chunk list into a vector
    for (unsigned int i = 0; i <= (fileChunkResponse.length-1); i += UNSIGNED_INT_SIZE) { 
      unsigned int chunkNumber;
      memcpy(&chunkNumber, &fileChunkResponse.data[i], UNSIGNED_INT_SIZE);
      fileChunkList.push_back(chunkNumber);
    }
  }

  // store chunk list with associated server IP
  pair<const char *, vector<unsigned int>> newPair = make_pair(serverIP, fileChunkList);
  torrentData.serverPeerOwnedChunks.push_back(newPair);
  shutdown(peerSocket.sockfd, SHUT_RDWR);
}

void clientRequestFileChunkListFromServerPeer(peerSocketInfo &peerSocket, torrentData &torrentData, const char *serverIP, args peerArgs) {
  PacketHeader fileChunkListRequest;
  fileChunkListRequest.type = CHUNK_LIST_REQUEST;
  fileChunkListRequest.length = 0;
  cout << "CLIENT: Requesting file chunk list from peer..." << endl;
  send(peerSocket.sockfd, &fileChunkListRequest, sizeof(fileChunkListRequest), MSG_NOSIGNAL);

  // lock and log
  loggingMutex.lock();
  writeToLogFile(peerArgs.log, serverIP, CHUNK_LIST_REQUEST, fileChunkListRequest.length);
  loggingMutex.unlock();

  cout << "CLIENT: Requested file chunk list from peer..." << endl;
  clientReceiveFileChunkListFromServerPeer(peerSocket, torrentData, serverIP, peerArgs);
}

void clientReceiveFileChunkFromServerPeer(peerSocketInfo &peerSocket, torrentData &torrentData, unsigned int chunkCRCFromTorrentFile, unsigned int chunkNum, args peerArgs) {
  packet fileChunkResponse;
  cout << "CLIENT: Attempting to receive file chunk from peer..." << endl;
  recv(peerSocket.sockfd, &fileChunkResponse, HEADER_SIZE, MSG_WAITALL);
  cout << "I AM GOING TO RECEIVE A LENGTH OF " << fileChunkResponse.length << endl;
  if (fileChunkResponse.type == CHUNK_RESPONSE) {
    recv(peerSocket.sockfd, &fileChunkResponse.data, fileChunkResponse.length, MSG_WAITALL);

    // lock and log
    char *ip = inet_ntoa(peerSocket.server_addr.sin_addr);
    loggingMutex.lock();
    writeToLogFile(peerArgs.log, ip, CHUNK_RESPONSE, fileChunkResponse.length, chunkCRCFromTorrentFile);
    loggingMutex.unlock();

    // compute CRC of chunk received
    unsigned int receivedDataCRC = crc32(fileChunkResponse.data, fileChunkResponse.length); // computing CRC with length of data

    cout << endl << "CLIENT: Received file chunk packet data has size of " << fileChunkResponse.length << " bytes and the total packet size is " << sizeof(fileChunkResponse) << " bytes" << endl;
    cout << endl;
    cout << "CLIENT: Received CRC: " << receivedDataCRC << " for chunk: " << chunkNum << endl;
    cout << "CLIENT: Correct CRC: " << chunkCRCFromTorrentFile << endl;
    
    if (receivedDataCRC == chunkCRCFromTorrentFile) {
      cout << "CLIENT: Received correct chunk response for chunkNum " << chunkNum << " from server peer" << endl << endl;
      data chunkData;
      // initialize chunkData.data to 0
      memset(chunkData.data, 0, sizeof(chunkData.data));
      memcpy(chunkData.data, fileChunkResponse.data, fileChunkResponse.length);
      torrentData.finalChunkData.insert(pair<unsigned int, data>(chunkNum, chunkData));
      torrentData.chunkAndSize.insert(pair<unsigned int, unsigned int>(chunkNum, fileChunkResponse.length));
    } else { 
      cout << "#### CLIENT: Received chunk " << chunkNum << " from server peer but received CRC " << receivedDataCRC << " does not match real CRC " << chunkCRCFromTorrentFile << endl;
    }
  }
  shutdown(peerSocket.sockfd, SHUT_RDWR);
}

void clientRequestFileChunkFromServerPeer(peerSocketInfo &peerSocket, torrentData &torrentData, unsigned int chunkCRCFromTorrentFile, unsigned int chunkNumber, args peerArgs) {
  packet fileChunkRequest;
  fileChunkRequest.type = CHUNK_REQUEST;
  fileChunkRequest.length = UNSIGNED_INT_SIZE;
  memcpy(&fileChunkRequest.data[0], &chunkCRCFromTorrentFile, UNSIGNED_INT_SIZE);
  cout << "CLIENT: Requesting file chunk from peer..." << endl;
  send(peerSocket.sockfd, &fileChunkRequest, fileChunkRequest.length + HEADER_SIZE, MSG_NOSIGNAL);

  // lock and log
  char *ip = inet_ntoa(peerSocket.server_addr.sin_addr);
  loggingMutex.lock();
  writeToLogFile(peerArgs.log, ip, CHUNK_REQUEST, fileChunkRequest.length, chunkCRCFromTorrentFile);
  loggingMutex.unlock();

  cout << "CLIENT: Requested file chunk from peer..." << endl;
  clientReceiveFileChunkFromServerPeer(peerSocket, torrentData, chunkCRCFromTorrentFile, chunkNumber, peerArgs);
}

pair<unsigned int, unsigned int> getChunkWithSmallestOccurrence(map<unsigned int, unsigned int> nonOwnedChunksAndOccurrences) {
  unsigned int smallestChunkOccurence = UINT_MAX;
  unsigned int smallestChunkOccurenceChunk = 0;
  // Find the chunk with the smallest occurence
  for (auto it = nonOwnedChunksAndOccurrences.begin(); it != nonOwnedChunksAndOccurrences.end(); it++) {
    if (it->second < smallestChunkOccurence) {
      smallestChunkOccurence = it->second;
      smallestChunkOccurenceChunk = it->first;
    }
  }
  pair<unsigned int, unsigned int> chunkAndOccurrences = make_pair(smallestChunkOccurenceChunk, smallestChunkOccurence);
  return chunkAndOccurrences;
}

map<unsigned int, unsigned int> getPeerChunkOccurrences(torrentData &torrentData) {
  map<unsigned int, unsigned int> chunkCount; // chunk number, count
  for (unsigned int i = 0; i < torrentData.serverPeerOwnedChunks.size(); i++) {
    for (unsigned int j = 0; j < torrentData.serverPeerOwnedChunks[i].second.size(); j++) {
      // only consider chunk if it is not in the ownedChunks vector
      if (find(torrentData.ownedChunks.begin(), torrentData.ownedChunks.end(), torrentData.serverPeerOwnedChunks[i].second[j]) == torrentData.ownedChunks.end()) {
        chunkCount[torrentData.serverPeerOwnedChunks[i].second[j]]++;
      }
    }
  }
  return chunkCount;
}

void clientHandleServerPeerChunkRequests(torrentData &torrentData, args peerArgs) {
  while (torrentData.nonOwnedChunksAndOccurrences.size() > 0) {
    pair<unsigned int, unsigned int> chunkAndOccurrences = getChunkWithSmallestOccurrence(torrentData.nonOwnedChunksAndOccurrences);
    unsigned int chunkWithSmallestOccurence = chunkAndOccurrences.first;
    unsigned int smallestChunkOccurences = chunkAndOccurrences.second;

    cout << endl << "CLIENT: There are " << torrentData.nonOwnedChunksAndOccurrences.size() << " non-owned chunks left to get" << endl;
    cout << "CLIENT: Smallest chunk occurence is " << smallestChunkOccurences << " for chunk " << chunkWithSmallestOccurence << endl;

    bool gotChunk = false;
    while (!gotChunk) {
      // find the server peer that owns the chunk
      for (unsigned int i = 0; i < torrentData.serverPeerOwnedChunks.size(); i++) { // for each IP
        const char *serverIP = torrentData.serverPeerOwnedChunks[i].first;
        for (unsigned int j = 0; j < torrentData.serverPeerOwnedChunks[i].second.size(); j++) { // for each chunk owned by that IP
          unsigned int chunkNumberOwnedByServer = torrentData.serverPeerOwnedChunks[i].second[j];
          if (chunkNumberOwnedByServer == chunkWithSmallestOccurence) { // found a server peer that owns the chunk
            cout << "CLIENT: Server peer " << serverIP << " owns chunk " << chunkWithSmallestOccurence << endl;
            unsigned int chunkCRC = torrentData.chunkList[chunkWithSmallestOccurence];
            cout << "CLIENT: Sending CRC " << chunkCRC << " to peer " << serverIP << endl;
            peerSocketInfo socketInfo = connectToServerPeer(peerArgs.myIP, serverIP);
            clientRequestFileChunkFromServerPeer(socketInfo, torrentData, chunkCRC, chunkNumberOwnedByServer, peerArgs);
            torrentData.nonOwnedChunksAndOccurrences.erase(chunkWithSmallestOccurence); // remove the chunk from the nonOwnedChunkOccurences vector
            gotChunk = true;
            break;
          }
        }
        if (gotChunk) {
          break;
        }
      }
    }
  }
  cout << "CLIENT: *** Received all required chunks from server***" << endl;
  cout << "CLIENT: *** Length of owned chunks is " << torrentData.finalChunkData.size() << " ***" << endl;
}

void connectToEachServerPeerAndRequest(args peerArgs, torrentData &torrentData, bool receivedChunkList) {
  if (!receivedChunkList) {
    for (unsigned int i = 0; i < torrentData.peerList.size(); i++) {
      if (peerArgs.myIP != torrentData.peerList[i]) {
        const char *serverIP = torrentData.peerList[i].c_str();
        peerSocketInfo clientSocketInfo = connectToServerPeer(peerArgs.myIP, serverIP);
        // torrentData.peerClientSockets.push_back(clientSocketInfo);
        clientRequestFileChunkListFromServerPeer(clientSocketInfo, torrentData, serverIP, peerArgs); 
      }
    }
    cout << endl << "******************** CLIENT: Finished getting chunk list from all peers ********************" << endl << endl;
  }
  else {
    clientHandleServerPeerChunkRequests(torrentData, peerArgs);
  }
}

void readTorrentDataChunksToOutputFile(torrentData &torrentData) {
  cout << "CLIENT: *** Writing to output file ***" << endl;
  // write the final chunk data to the output file
  ofstream outputFile;
  outputFile.open(torrentData.outputFile, ios::out | ios::binary | ios::app); // appending
  //map<unsigned int, data> finalChunkData
  for (unsigned int i = 0; i < torrentData.finalChunkData.size(); i++) {
    size_t before = outputFile.tellp();
    cout << "Writing chunk " << i << " which is size " << torrentData.chunkAndSize[i] << endl;
    cout << "Starting at position: " << before << endl;
    //outputFile.write(torrentData.finalChunkData[i].data, sizeof(torrentData.finalChunkData[i].data));
    outputFile.write(torrentData.finalChunkData[i].data, torrentData.chunkAndSize[i]);
    size_t after = outputFile.tellp();
    cout << "Finished writing at position: " << after << " for a difference of " << after - before << endl;
  }
  outputFile.close();
}

int main(int argc, char* argv[]) 
{	
  // PEER
  // ./peer <my-ip> <tracker-ip> <input-file> <owned-chunks> <output-file> <log> 
  args peerArgs = retrieveArgs(argv);

  peerSocketInfo peerSocket = connectToTracker(peerArgs.myIP, peerArgs.trackerIP);

  requestTorrentFileFromTracker(peerSocket, peerArgs);
  packet torrentFilePacket = receiveTorrentFileFromTracker(peerSocket, peerArgs);
  // orderly shutdown of the socket
  shutdown(peerSocket.sockfd, SHUT_RDWR);

  torrentData torrentData = parseTorrentFile(torrentFilePacket.data);
  torrentData.ownedChunksFile = peerArgs.ownedChunks;
  torrentData.inputFile = peerArgs.inputFile;
  torrentData.outputFile = peerArgs.outputFile;
  generateOtherPeerList(torrentData, peerArgs.myIP);

  // determine which chunks to request
  getOwnedChunksFromFile(torrentData);
  determineNeededChunks(torrentData);
  readOwnedChunksIntoTorrentData(torrentData);

  // setup peer server socket 
  peerServerInfo peerServerSocket = setupServerPeerSocketToListen();

  // 1. Accept request, 2. Spawn new thread to handle request 3. Start waiting to accept again
  vector<thread> threads;
  thread acceptingPeers(serverAcceptClientPeerConnection, ref(peerServerSocket), ref(torrentData), ref(threads), ref(peerArgs));

  // sequentially connect to each peer for chunk list
  bool receivedChunkList = false;
  connectToEachServerPeerAndRequest(peerArgs, torrentData, receivedChunkList);
  receivedChunkList = true;
  // Map contains <chunk number, count> of chunks owned by peers
  torrentData.nonOwnedChunksAndOccurrences = getPeerChunkOccurrences(torrentData);

  // output chunk count map
  cout << "CLIENT: Non-Owned Chunk count map:" << endl;
  for (auto it = torrentData.nonOwnedChunksAndOccurrences.begin(); it != torrentData.nonOwnedChunksAndOccurrences.end(); it++) {
    cout << "Chunk: " << it->first << " - " << "Occurrences of chunk on network: " << it->second << endl;
  }

  // sequentially connect to each peer for chunks
  connectToEachServerPeerAndRequest(peerArgs, torrentData, receivedChunkList);

  readTorrentDataChunksToOutputFile(torrentData);

  cout << "*************** DONE ***************" << endl;
  acceptingPeers.join(); // this never joins, peers stay running
  
}
