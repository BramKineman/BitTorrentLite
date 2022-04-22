#include <mutex>
#include <iostream>

std::mutex loggingMutex;

using namespace std;

int randomFunction() {

  cout << "I AM IN ANOTHER FILE WOWOOWOW" << endl;

  return 0;
  
}
