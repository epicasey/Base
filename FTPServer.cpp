/*
 * FTPServer.cpp
 *
 *  Created on: Oct 18, 2024
 *  Ported to ESP32 on 2025-10-14
 *  Updated to ESP verson 6.0.1 on 2026-06-17
 *      Author: Ed Casey
 */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <algorithm>
#include <cctype>
#include "NVStorage.h"
//#include <esp_log.h> //in Common.h from FTPServer.h
#include "FTPServer.h"

// raise log level to warning to avoid info messages from lwip sockets
#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_WARN

static void ltrim(std::string &s) {// trim from start (in place)
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !std::isspace(ch);
	}));
} // ltrim

static void rtrim(std::string &s) { // trim from end (in place)
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !std::isspace(ch);
	}).base(), s.end());
} // rtrim

static void trim(std::string &s) {// trim from both ends (in place)
	ltrim(s);
	rtrim(s);
} // trim

WorkingDirectory FTPServer::cwd;

FTPServer::FTPServer(const std::string &mount_point) {
	cwd = WorkingDirectory(mount_point);
	COUT<<"Mounted file system "<<cwd.path(WorkingDirectory::VFS)<<std::endl;
	std::string id(NVS.cread("ftp_usr"));
	std::string pw(NVS.cread("ftp_pwd"));
	if(id != "NOT_FOUND" && pw != "NOT_FOUND") {
	m_userid = id;
	m_password = pw;
	m_loginRequired = !(m_userid.empty() && m_password.empty());
	m_isAuthenticated = false;
		COUT << "FTP userid: " << m_userid << " password: " << (m_password.empty() ? "<empty>" : "********") << std::endl;
	} else {
		COUT << "FTP credentials not found" << std::endl; 
	}
} // FTPServer#FTPServer

FTPServer::~FTPServer() {// Nothing to do here by default.
} // FTPServer#~FTPServer

void FTPServer::closeConnection() {//Close the connection to the FTP client.
	close(m_clientSocket);
} // FTPServer#closeConnection

void FTPServer::closeData() {//Close a previously opened data connection.
	close(m_dataSocket);
	m_dataSocket = -1;
} // FTPServer#closeData

void FTPServer::closePassive() {//Close the passive listening socket that was opened by listenPassive.
	close(m_passiveSocket);
	m_passiveSocket = -1;
} // FTPServer#closePassive

std::string FTPServer::listenPassive() {//Create a listening socket for the new passive connection.
	m_passiveSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_passiveSocket == -1) {
		LOGE("socket: %s", strerror(errno));
	}
	struct sockaddr_in clientAddrInfo;
	socklen_t addrInfoSize = sizeof(clientAddrInfo);
	getsockname(m_clientSocket, (struct sockaddr*) &clientAddrInfo, &addrInfoSize);
	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(0);
	int rc = bind(m_passiveSocket, (struct sockaddr*) &serverAddress, sizeof(serverAddress));
	if (rc == -1) {
		LOGE("bind: %s", strerror(errno));
	}
	rc = listen(m_passiveSocket, 5);
	if (rc == -1) {
		LOGE("listen: %s", strerror(errno));
	}
	socklen_t addrLen = sizeof(serverAddress);
	rc = getsockname(m_passiveSocket, (struct sockaddr*)&serverAddress, &addrLen);
	if (rc == -1) {
		LOGE("getsockname: %s", strerror(errno));
	}
	std::stringstream ss;
	ss << ((clientAddrInfo.sin_addr.s_addr >> 0) & 0xff) <<
		"," << ((clientAddrInfo.sin_addr.s_addr >> 8) & 0xff) <<
		"," << ((clientAddrInfo.sin_addr.s_addr >> 16) & 0xff) <<
		"," << ((clientAddrInfo.sin_addr.s_addr >> 24) & 0xff) <<
		"," << ((serverAddress.sin_port >> 0) & 0xff) <<
		"," << ((serverAddress.sin_port >> 8) & 0xff);
	std::string retStr = ss.str();
	return retStr;
} // FTPServer#listenPassive

bool FTPServer::openData() {//Open a data connection with the client.  We will use closeData() to close the connection.
	if (m_isPassive) {
		struct sockaddr_in clientAddress;
		socklen_t clientAddressLength = sizeof(clientAddress); //connect to client
		m_dataSocket = accept(m_passiveSocket, (struct sockaddr *)&clientAddress, &clientAddressLength);
		if (m_dataSocket == -1) {
			closePassive();
			return false;
		}
		closePassive();
	} else {
		m_dataSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //connect to client
		struct sockaddr_in serverAddress;
		serverAddress.sin_family      = AF_INET;
		serverAddress.sin_addr.s_addr = htonl(m_dataIp);
		serverAddress.sin_port        = htons(m_dataPort);
		int rc = connect(m_dataSocket, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in));
		if (rc == -1) {
			LOGW("FTPServer::openData: connect(): %s", strerror(errno));
			return false;
		}
	}
	return true;
} // FTPServer#openData

void FTPServer::processCommand() {//Process commands received from the client.
	exitFlag = 3;
	sendResponse(FTPServer::RESPONSE_220_SERVICE_READY); // Service ready.
	LOGI(">> FTPServer::processCommand");
	m_lastCommand = "";
	COUT<< "Enter process loop"<<std::endl;
	while (true) {  //Processing loop
		int rc {0};
		std::vector<char> buffer;
		while (true) {// Read until newline with dynamic buffer size;
			buffer.emplace_back('\0');
			rc = recv(m_clientSocket, &buffer.back(), 1, 0);
			if (rc <= 0) break; // Connection closed or error
			if (buffer.back() == '\n') break; // Stop when newline is received
		}
		if (rc == 0) {COUT << "Connection closed" << std::endl; break;} // Connection closed.
		if (rc == -1 && errno == ECONNRESET) {COUT << "Connection reset by peer (RST)" << std::endl; break;}
		if (rc == -1 ) {COUT << "Error " << strerror(errno) << std::endl; break;}

		std::string command(buffer.begin(),buffer.end());
		std::istringstream ss(command.substr(0, command.find("\r\n")));
		getline(ss, command, ' ');
		trim(command);
		if(m_loginRequired && !m_isAuthenticated && ((command!="USER") && (command!="PASS"))){
			sendResponse(FTPServer::RESPONSE_530_NOT_LOGGED_IN);
			continue;
		}
		// We now have a command to process.
		LOGI("Command: \"%s\" with \"%s\"", command.c_str(),ss.str().c_str());
		auto cmdOk = cmdMap.find(command);
		if (cmdOk != cmdMap.end()) cmdOk->second(ss);
		else {
			sendResponse(FTPServer::RESPONSE_500_COMMAND_UNRECOGNIZED); // Syntax error, command unrecognized.
			LOGI("Command \"%s\" not recognized",command.c_str());
		}
		m_lastCommand = command;
	} // End loop processing commands.
	COUT<<"Exiting Proceess Loop"<<std::endl;
	close(m_clientSocket); // We won't be processing any further commands from this client.
	m_isAuthenticated = false;
} // FTPServer::processCommand

void FTPServer::receiveFile(std::string fileName) {//Receive a file from the FTP client (STOR).  The name of the file to be created is passed as a parameter.
	if (m_callbacks != nullptr) {
		try {
			m_callbacks->onStoreStart(fileName);
		} catch(FTPServer::FileException& e) {
			LOGE("Caught a file exception storing %s",fileName.c_str());
			sendResponse(FTPServer::RESPONSE_550_ACTION_NOT_TAKEN); // Requested action not taken.
			return;
		}
	}
	if (!openData()) {
		sendResponse(FTPServer::RESPONSE_425_CANT_OPEN_DATA_CONNECTION); // Can't open data connection.
		LOGE("Failed to open data connection");
		return;
	}
	sendResponse(FTPServer::RESPONSE_150_ABOUT_TO_OPEN_DATA_CONNECTION); // File status okay; about to open data connection.
	std::vector<uint8_t> buf(m_chunkSize);
	uint32_t totalBytesRead = 0;
	while (true) {
		int rc = recv(m_dataSocket, buf.data(), m_chunkSize, 0);
		if (rc == 0) {
			LOGI("Connection closed by client after %lu bytes for %s", totalBytesRead, fileName.c_str());
			break;
		}
		if (rc == -1) {
			LOGE("Error receiving data: %s", strerror(errno));
			closeData();
			break;
		}
		if (m_callbacks != nullptr) {
			m_callbacks->onStoreData(buf.data(), rc);
		}
		totalBytesRead += rc;
	}
	sendResponse(FTPServer::RESPONSE_226_CLOSING_DATA_CONNECTION); // Closing data connection.
	closeData();
	if (m_callbacks != nullptr) {
		m_callbacks->onStoreEnd();
	}
	LOGI("Received %lu bytes for %s", totalBytesRead, fileName.c_str());
} // FTPServer#receiveFile

void FTPServer::sendData(uint8_t* pData, uint32_t size) {//Send data to the client over the data connection previously opened with a call to openData().
	int rc = send(m_dataSocket, pData, size, 0);
	if (rc == -1) {
		LOGW("FTPServer::sendData: send(): %s", strerror(errno));
	}
} // FTPServer#sendData


/**
 * Send a response to the client.  A response is composed of two parts.  The first is a code as architected in the
 * FTP specification.  The second is a piece of text.
 */
void FTPServer::sendResponse(int code,std::string text) {
	std::ostringstream ss;
	ss << code << " " << (text.size() != 0?text:Cmd_txt.lookupCmdText(code)) << "\r\n";
	int rc = send(m_clientSocket, ss.str().data(), ss.str().length(), 0);
	if (rc == -1) {
		LOGW("send: %s  %d", strerror(errno),code);
	}
} // FTPServer#sendResponse

void FTPServer::setCallbacks(std::unique_ptr<FTPCallbacks> pCallbacks) {//Set the callbacks that are to be invoked to perform work.
	m_callbacks = std::move(pCallbacks);
} // FTPServer#setCallbacks

void FTPServer::setCredentials(std::string userid, std::string password) {
	m_loginRequired = true;
	m_userid        = userid;
	m_password      = password;
} // FTPServer#setCredentials

void FTPServer::setPort(uint16_t port) {//Set the TCP port we should listen on for FTP client requests.
	m_port = port;
} // FTPServer#setPort

void FTPServer::start() {// Start being an FTP Server.
	m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_serverSocket == -1) {
		LOGE("socket: %s", strerror(errno));
	}
	int enable = 1;
	setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(m_port);
	int rc = bind(m_serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
	if (rc == -1) {
		LOGE("bind: %s", strerror(errno));
	}
	rc = listen(m_serverSocket, 5);
	if (rc == -1) {
		LOGE("listen: %s", strerror(errno));
		return; // Stop server start if listen fails
	}
	while (true) {
		waitForFTPClient();
		processCommand();
		High_Water1
	}
} // FTPServer#start

int FTPServer::waitForFTPClient() {//Wait for a new client to connect.
	LOGI(">> FTPServer::waitForFTPClient");
	struct sockaddr_in clientAddress;
	socklen_t clientAddressLength = sizeof(clientAddress);
	m_clientSocket = accept(m_serverSocket, (struct sockaddr*) &clientAddress, &clientAddressLength);
	char ipAddr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddress.sin_addr, ipAddr, sizeof(ipAddr));
	struct sockaddr_in socketAddressInfo;
	socklen_t socketAddressInfoSize = sizeof(socketAddressInfo);
	getsockname(m_clientSocket, (struct sockaddr*) &socketAddressInfo, &socketAddressInfoSize);
	inet_ntop(AF_INET, &socketAddressInfo.sin_addr, ipAddr, sizeof(ipAddr));
	return m_clientSocket;
} // FTPServer::waitForFTPClient