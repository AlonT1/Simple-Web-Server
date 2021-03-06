//#include "pch.h" // please enable if visual studio is configured to work with precompiled headers
#include <iostream>
#include <WS2tcpip.h>
#include <iterator>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include<ctime>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

////////////////error checking macro - to avoid repeating code
#define SOCKET_ERROR_MSG(connSocket, errorMSG){\
	cout << errorMSG << WSAGetLastError() << endl;\
	closesocket(connSocket);\
	WSACleanup();\
	return(-1);\
}

//function definitions
int readRequest(const char*, string&, string&, string&, int&);
int respondToClient(SOCKET&, string&, string&, string&,  int&);
string generateTime();



int main()
{
	WSAData wsaData; //init socket
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "Time Server: Error at WSAStartup()\n";
		return -1;
	}

	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //init listen socket

	if (INVALID_SOCKET == listenSocket)
		SOCKET_ERROR_MSG(listenSocket, "Time Server: Error at socket(): ");

	unsigned long flag = 1;
	if (ioctlsocket(listenSocket, FIONBIO, &flag) != 0)
		SOCKET_ERROR_MSG(listenSocket, "Time Server: Error at ioctlsocket(): ");

	sockaddr_in serverService; //server address
	serverService.sin_family = AF_INET;
	serverService.sin_addr.s_addr = INADDR_ANY;
	serverService.sin_port = htons(27015);

	//bind socket to server address 
	if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService)))
		SOCKET_ERROR_MSG(listenSocket, "Time Server: Error at bind(): ");

	//listen for incoming connections through our listen socket
	if (SOCKET_ERROR == listen(listenSocket, 5))
		SOCKET_ERROR_MSG(listenSocket, "Time Server: Error at listen(): ");

	//setup socket fd array, zero it out and push the listening socket in
	fd_set sockArr;
	FD_ZERO(&sockArr);
	FD_SET(listenSocket, &sockArr);

	while (true)
	{
		//each iteration select() func overwrites the fd_set it's operating on - may lead to socket loss
		//therefore, new sockets are added to original sockArr but the actual select and for loop operates on a copy
		//in this way the original set with it's added sockets are kept safe through each iteration
		fd_set backupArr = sockArr;
		int totalSockets = select(0, &backupArr, NULL, NULL, NULL);

		for (int i = 0; i < totalSockets; i++)
		{
			SOCKET iteratedSocket = backupArr.fd_array[i];

			if (listenSocket == iteratedSocket) //is this the listener? then check and accept new clients
			{
				SOCKET client = accept(listenSocket, NULL, NULL);
				if (ioctlsocket(client, FIONBIO, &flag) != 0)
					SOCKET_ERROR_MSG(listenSocket, "Time Server: Error at ioctlsocket(): ");
				FD_SET(client, &sockArr);
			}
			else // Receive message from browser (=client)
			{
				char msgRecv[4096];
				ZeroMemory(msgRecv, 4096);
				if (recv(iteratedSocket, msgRecv, 4096, 0) <= 0)
				{
					closesocket(iteratedSocket);
					FD_CLR(iteratedSocket, &sockArr);
				}
				else //if msg was properly recieved, find out the request of the client and 
				{
					string requestedContent = ""; //text or html content inside the file
					string requestType = ""; // PUT, HEAD, GET
					string fileType = "";	//txt or html
					int responseCode; // one of the following: 200, 204, 201, 404
					readRequest(msgRecv, requestedContent, requestType, fileType, responseCode);
					respondToClient(iteratedSocket, requestedContent, requestType, fileType, responseCode);
				}
			}
		}
	}
	return 1;
}

//grab the the html code from the requested file(if exists), and find out the requestType (GET, HEAD, PUT)
int readRequest(const char* msgRecv, string& requestedContent, string& requestType, string& fileType, int& responseCode)
{
	cout << "The request from client:\n-----------------------"<<endl;
	cout << msgRecv << endl <<endl;
	istringstream requestMSG(msgRecv);
	vector<string> msg2Arr((istream_iterator<string>(requestMSG)), istream_iterator<string>()); //break down the message into array
	requestType = msg2Arr[0]; // grab browser request: GET/HEAD/PUT
	//if no specific page reqeusted("/") assume homepage otherwise grab the name of the requested page
	string fileName = (msg2Arr[1] == "/") ? "index.html" : msg2Arr[1].substr(1); //substr to remove the "/" at the start of file name  
	fileType = fileName.find(".html")!= string::npos ? ("html") : ("txt"); 
	ifstream file(fileName); //open requested html file in "read mode"
	bool isFileExists = file.good();
	if (!isFileExists && requestType != "PUT") //requested page doesn't exist and the client doesn't want to "PUT" it 
	{
		responseCode = 404;
		return -1; // if indeed the page doesn't exist - quit
	}
	else if (requestType == "GET")
	{
		requestedContent = string(istreambuf_iterator<char>(file), istreambuf_iterator<char>());//grab the code from requested file
		responseCode = 200;
	}
	else if (requestType == "HEAD")
	{
		//in HEAD we grab the requested content just to calculate it's content-length later in the response - the content won't be displayed
		requestedContent = string(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
		responseCode = 200;
	}
	else if (requestType == "PUT")
	{
		int beginOfPutBody = requestMSG.str().find("\r\n\r\n")+4; //detecting pos of empty line between header and body
		string putBody = requestMSG.str().substr(beginOfPutBody);

		if (!isFileExists) // file doesn't exist -> create new file 
		{
			ofstream newFile;
			newFile.open(fileName, ofstream::out); //create the file  
			if (!newFile)
			{
				cout << "error creating page" << endl;
				return -1;
			}
			newFile << putBody;
			responseCode = 201; //new file created
			requestedContent = putBody;
			newFile.close();
		}
		else if (isFileExists)
		{
			requestedContent = string(istreambuf_iterator<char>(file), istreambuf_iterator<char>());//grab the code inside HTML
			if (putBody == requestedContent)
				responseCode = 204; // PUT update didn't change anything, since the code in PUT and in requested HTML is the same
			else if (putBody != requestedContent) // if PUT body is different than HTML Code - replace it with PUT Body
			{
				ofstream file(fileName, ios::out | ios::trunc); //open file in "write mode" and delete everything in it
				file << putBody; //replace the deleted body with the PUT body
				requestedContent = putBody;
				responseCode = 200;
			}
		}
	}
	file.close();
	return 1;
}

//send HTTP response to client
int respondToClient(SOCKET& client, string& requestedContent, string& requestType, string& fileType, int& responseCode)
{
	// Write the document back to the client
	string resposeDescription;
	switch (responseCode)
	{
	case 200:
		resposeDescription = "200 OK";
		break;
	case 201:
		resposeDescription = "201 Created";
		break;
	case 204:
		resposeDescription = "204 No Content";
		break;
	case 404:
		resposeDescription = "404 Not Found";
		break;
	}
	ostringstream response;
	response << "HTTP/1.1 " << resposeDescription << "\r\n";
	response << "Date: " << generateTime();
	response << "Server: Windows 10 (Win64)\r\n";
	response << "Content-Length: " << requestedContent.size() << "\r\n";
	response << "Connection: close \r\n";
	response << "Content-Type: "<< ( (fileType=="html") ? ("text/html;") : ("text/plain;") ) << " charset=utf-8 \r\n";
	response << "\r\n";
	if (requestType == "GET" || requestType == "PUT") //note: if requestType==HEAD, content won't be shown in response
		response << requestedContent;

	cout << "The response from server:\n-----------------------" << endl;
	cout << response.str() << endl;
	if (SOCKET_ERROR == send(client, response.str().c_str(), (response.str().size() + 1), 0))
		SOCKET_ERROR_MSG(client, "Time Client: Error at sendTo(): ");
	return 1;
}


//generates a time in UTC format for the http response made by the server
string generateTime()
{
	__time32_t aclock;
	_time32(&aclock);
	char currentTime[32]; //holds the final time text
	struct tm timeStruct;
	_localtime32_s(&timeStruct, &aclock);   //clock to time struct
	asctime_s(currentTime, 32, &timeStruct); //time struct to string - into buffer
	return currentTime;
}