all: tracker peer

tracker: tracker.o
	g++ tracker.o -o tracker

peer: peer.o
	g++ peer.o -o peer

tracker.o: tracker.cpp
	g++ -c -Wall tracker.cpp

peer.o: peer.cpp
	g++ -c -Wall peer.cpp

clean: 
	rm tracker.o tracker peer.o peer
