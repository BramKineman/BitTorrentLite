#include <mutex>
#include <iostream>
#include <fstream>

std::mutex loggingMutex;

using namespace std;

void writeToLogFile(char* logFile, const char* iPAddress, unsigned int packetType, unsigned int packetLength, unsigned int packetCRC='\0') {
  // if CRC is null, make it empty string
  string packetCRCString = "";
  if (packetCRC != '\0') {
    packetCRCString = to_string(packetCRC);
  }
  ofstream logFileStream;
  logFileStream.open(logFile, ios::app);
  logFileStream << iPAddress << " " << packetType << " " << packetLength << " " << packetCRC << endl;
  logFileStream.close();
}
