/*
 * FTPServer.cpp
 *
 *  Created on: Oct 18, 2024
 *  Ported to ESP32 on 2025-10-14
 *  Updated to ESP verson 6.0.1 on 2026-06-17
 *      Author: Ed Casey
 */
#ifndef NETWORKING_FTPSERVER_FTPSERVER_H_
#define NETWORKING_FTPSERVER_FTPSERVER_H_

#include <stdint.h>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <unordered_map>
#include <functional>
#include <sys/stat.h>
#include <memory>
//#include "Common.h"
#include "base.h"

class WorkingDirectory {
private:
	std::vector<std::string> vPath;
	std::string sPath;
public:
	enum type { VFS, FTP };

	WorkingDirectory() = default;
	WorkingDirectory(const std::string& mount_point) {
		sPath = mount_point[0] == '/' ? mount_point.substr(1) : mount_point;
		vPath = {sPath, ""};
	}
	void cupd() {
		chdir("..");
	}

	void chdir(std::string newPath) {
		if (newPath.size() == 0) return;  // newPath is empty
		std::replace(newPath.begin(), newPath.end(), '\\', '/');
		COUT << "Ftp Directory In: " << newPath << std::endl;
		COUT << "FTP Current newPath: " << path(FTP) << std::endl;
		if (newPath.find("..") != std::string::npos && vPath.size() > 1) { // go up one level
			COUT << "POP " << vPath.back() << std::endl;
			vPath.pop_back();
		} else {
			if (newPath.substr(0, 2) == "./") newPath = newPath.substr(1);
			std::stringstream ss(newPath);
			std::string temp_str;
			while (getline(ss, temp_str, '/')) {
				COUT << "getline = " << temp_str << std::endl;
				if (temp_str.size() > 0 && temp_str != vPath.back()) {
					vPath.emplace_back(temp_str);
				}
			}
		}
		COUT << "New Ftp Path: " << path(FTP) << std::endl;
		COUT << "New Fs Path: " << path(VFS) << std::endl;
	}

	const char* path(int fs) {
		sPath.clear();
		for (auto it = vPath.begin() + fs; it != vPath.end(); ++it)
			if (!it->empty()) sPath += "/" + *it;
		if (sPath.empty()) sPath = "/";
		return sPath.c_str();
	}
	const char* VFShome() {
		static std::string home = "/" + vPath.front();
		return home.c_str();
	}
};

#define FTPCallback_NOT_IMPLEMENTED LOGE("FTPCallbacks::%s not implemented",__func__ )

class FTPCallbacks {
public:
	const char* FTPCallback_TAG = "FTPCallbacks";
	virtual void   onStoreStart(std::string fileName)			  {FTPCallback_NOT_IMPLEMENTED;};
	virtual size_t onStoreData(uint8_t* data, size_t size)	  {FTPCallback_NOT_IMPLEMENTED; return 0;};
	virtual void   onStoreEnd()								  {FTPCallback_NOT_IMPLEMENTED;};
	virtual void   onRetrieveStart(std::string fileName)			  {FTPCallback_NOT_IMPLEMENTED;};
	virtual size_t onRetrieveData(uint8_t *data, size_t size) {FTPCallback_NOT_IMPLEMENTED; return 0;};
	virtual void   onRetrieveEnd()							  {FTPCallback_NOT_IMPLEMENTED;};
	virtual std::string onDir()									  {FTPCallback_NOT_IMPLEMENTED; return "";};
	virtual ~FTPCallbacks(){};
};

class FTPFileCallbacks : public FTPCallbacks {//An implementation of FTPCallbacks that uses Posix File I/O to perform file access.
public:
	void   onStoreStart(std::string fileName) override;				// Called for a STOR request.
	size_t onStoreData(uint8_t* data, size_t size) override;	// Called when a chunk of STOR data becomes available.
	void   onStoreEnd() override;								// Called at the end of a STOR request.
	void   onRetrieveStart(std::string fileName) override;			// Called at the start of a RETR request.
	size_t onRetrieveData(uint8_t* data, size_t size) override;	// Called to retrieve a chunk of RETR data.
	void   onRetrieveEnd() override;							// Called when we have retrieved all the data.
	std::string onDir() override;									// Called to retrieve all the directory entries.

private:
	std::ofstream m_storeFile;	 // File used to store data from the client.
	std::ifstream m_retrieveFile; // File used to retrieve data for the client.
	uint32_t m_byteCount;	 // Count of bytes sent over wire.
};

class FTPServer {
public:
	FTPServer(const std::string &mount_point = {});
	virtual ~FTPServer();
	void setCredentials(std::string userid, std::string password);
	void start();
	void setPort(uint16_t port);
	void setCallbacks(std::unique_ptr<FTPCallbacks> pFTPCallbacks);
	static WorkingDirectory cwd;
	class FileException: public std::exception {
	public:
		FileException() {}
		FileException(const std::string& file, int err, const std::string& msg)
			: fileName(file), errorCode(err), errorMsg(msg) {
				if(fileName.size()>maxFilenameLength)
				LOGE("Filename too long: %s",fileName.c_str());
			}
	private:
		std::string fileName;
		int errorCode;
		std::string errorMsg;
	#ifdef CONFIG_FATFS_LONG_FILENAMES
		int maxFilenameLength{255};
	#else
		int maxFilenameLength{11};
	#endif
	};
private:
	int restartMarker{0};
	int exitFlag{0};
	// Response codes.
	static constexpr int RESPONSE_150_ABOUT_TO_OPEN_DATA_CONNECTION = 150;
	static constexpr int RESPONSE_200_COMMAND_OK					= 200;
//	static constexpr int RESPONSE_202_COMMAND_NOT_IMPLEMENTED	    = 202;
//	static constexpr int RESPONSE_212_DIRECTORY_STATUS			    = 212;
//	static constexpr int RESPONSE_213_FILE_STATUS				    = 213;
//	static constexpr int RESPONSE_214_HELP_MESSAGE				    = 214;
	static constexpr int RESPONSE_215_UNIX_TYPE_L8					= 215;
	static constexpr int RESPONSE_220_SERVICE_READY				    = 220;
	static constexpr int RESPONSE_221_CLOSING_CONTROL_CONNECTION	= 221;
	static constexpr int RESPONSE_230_USER_LOGGED_IN				= 230;
	static constexpr int RESPONSE_226_CLOSING_DATA_CONNECTION	    = 226;
	static constexpr int RESPONSE_227_ENTERING_PASSIVE_MODE		    = 227;
	static constexpr int RESPONSE_250_FILE_ACTION_OK				= 250;
	static constexpr int RESPONSE_331_PASSWORD_REQUIRED			    = 331;
//	static constexpr int RESPONSE_332_NEED_ACCOUNT				    = 332;
	static constexpr int RESPONSE_350_RESTART_MARKER_SET			= 350;
	static constexpr int RESPONSE_425_CANT_OPEN_DATA_CONNECTION	    = 425;
	static constexpr int RESPONSE_500_COMMAND_UNRECOGNIZED		    = 500;
	static constexpr int RESPONSE_502_COMMAND_NOT_IMPLEMENTED	    = 502;
	static constexpr int RESPONSE_503_BAD_SEQUENCE				    = 503;
	static constexpr int RESPONSE_530_NOT_LOGGED_IN				    = 530;
	static constexpr int RESPONSE_550_ACTION_NOT_TAKEN			    = 550;
//	static constexpr int RESPONSE_553_FILE_NAME_NOT_ALLOWED		    = 553;

	struct CmdTextEntry {
    	int code;
    	const char* text;
	const char* lookupCmdText(int code) {
    for (const auto& entry : cmdTextTable) {
        if (entry.code == code)
            return entry.text;
    }
    return "Unknown code";
	}
	}Cmd_txt;

	static constexpr CmdTextEntry cmdTextTable[] = {
		{RESPONSE_150_ABOUT_TO_OPEN_DATA_CONNECTION,"File status okay; about to open data connection."},
		{RESPONSE_200_COMMAND_OK,					"Command okay."},
		{RESPONSE_215_UNIX_TYPE_L8,					"UNIX Type: L8"},
		{RESPONSE_220_SERVICE_READY,				"Service ready."},
		{RESPONSE_221_CLOSING_CONTROL_CONNECTION,	"Service closing control connection."},
		{RESPONSE_226_CLOSING_DATA_CONNECTION,		"Closing data connection."},
		{RESPONSE_230_USER_LOGGED_IN,				"User logged in, proceed."},
		{RESPONSE_227_ENTERING_PASSIVE_MODE,		"Entering Passive Mode."},
		{RESPONSE_250_FILE_ACTION_OK,				"Requested file action okay, completed."},
		{RESPONSE_331_PASSWORD_REQUIRED,			"Password required."},
		{RESPONSE_350_RESTART_MARKER_SET,			"Restart marker set."},
		{RESPONSE_425_CANT_OPEN_DATA_CONNECTION,	"Can't open data connection."},
		{RESPONSE_500_COMMAND_UNRECOGNIZED,			"Syntax error, command unrecognized."},
		{RESPONSE_502_COMMAND_NOT_IMPLEMENTED,		"Command not implemented."},
		{RESPONSE_503_BAD_SEQUENCE,					"Bad sequence of commands."},
		{RESPONSE_530_NOT_LOGGED_IN,				"Not logged in."},
		{RESPONSE_550_ACTION_NOT_TAKEN,				"Requested action not taken."},
	};

	const char* LOG_TAG = "FTPServer";
	int         m_serverSocket 	  {-1};     // The socket the FTP server is listening on.
	int         m_clientSocket 	  {-1};     // The current client socket.
	int         m_dataSocket   	  {-1};     // The data socket.
	int         m_passiveSocket	  {-1};     // The socket on which the server is listening for passive FTP connections.
	uint16_t    m_port			  {21};	    // The port the FTP server will use.
	uint16_t    m_dataPort		  {0};      // The port for data connections. The default Server-PI port is 21
	uint32_t    m_dataIp		  {0};      // The ip address for data connections.
	bool        m_isPassive		  {false};  // Are we in passive mode?  If not, then we are in active mode.
	bool        m_isImage		  {true};   // Are we in image mode?
	bool        m_loginRequired	  {false};  // Do we required a login?
	bool        m_isAuthenticated {false};  // Have we authenticated?
	size_t      m_chunkSize		  {4096};   // The maximum chunk size.
	std::string 		m_userid		  {""};     // The required userid.
	std::string 		m_password		  {""};     // The required password.
	std::string 		m_suppliedUserid  {""};	    // The userid supplied from the USER command.
	std::string 		m_lastCommand	  {""};     // The last command that was processed.
	std::string 		m_renameFrom	  {""};     // The filename supplied from the RNFR command.
	std::unique_ptr<FTPCallbacks> m_callbacks      {nullptr};// The callbacks for processing.
	void closeConnection();
	void closeData();
	void closePassive();
	bool openData();
	void receiveFile (std::string fileName);
	void sendResponse(int code,std::string text="");
	void sendData    (uint8_t* pData, uint32_t size);
	std::string listenPassive();
	int waitForFTPClient();
	void processCommand();

#define NOT_IMPLEMENTED sendResponse(FTPServer::RESPONSE_502_COMMAND_NOT_IMPLEMENTED);
	
	const std::unordered_map<std::string,std::function<void(std::istringstream &ss)>> cmdMap{
	 //Command interpreter - lambda functions to respond to implemented functions/
	 //   unimplemented commands should return RESPONSE_502_COMMAND_NOT_IMPLEMENTED
//			{"ABOR",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"ACCT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"ADAT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"ALLO",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"APPE",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"AUTH",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"AVBL",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"CCC" ,[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"CDUP",[&](std::istringstream &ss){
				std::string fileName;
				ss >> fileName; 
				COUT<< fileName <<" from "<<cwd.path(WorkingDirectory::FTP)<<std::endl;
				fileName = std::string(cwd.path(WorkingDirectory::VFS))+"/"+fileName;
				cwd.cupd();
				sendResponse(FTPServer::RESPONSE_200_COMMAND_OK);
				exitFlag = 1;
			}},
//			{"CONF",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"CSID",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"CWD", [&](std::istringstream &ss){
				std::string path;
				ss >> path;
				COUT <<">> onCwd: path= " <<path<<std::endl;
				cwd.chdir(path);
				sendResponse(FTPServer::RESPONSE_200_COMMAND_OK);
			}},
			{"DELE",[&](std::istringstream &ss){
				std::string fileName;
				ss >> fileName; 
				fileName = cwd.VFShome() + fileName;
				remove(fileName.c_str());
				sendResponse(FTPServer::RESPONSE_200_COMMAND_OK);
			}},
//			{"DSIZ",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"ENC" ,[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"EPRT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"EPSV",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"FEAT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"HELP",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"HOST",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"LANG",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"LIST",[&](std::istringstream &ss){
				openData();
				sendResponse(FTPServer::RESPONSE_150_ABOUT_TO_OPEN_DATA_CONNECTION); // File status okay; about to open data connection.
				if (m_callbacks != nullptr) {
					std::string dirString = m_callbacks->onDir();
					COUT<<"Sending: "<<dirString.size()<<" bytes"<<std::endl;
					sendData((uint8_t*) dirString.data(), dirString.length());
				}
				closeData();
				sendResponse(FTPServer::RESPONSE_226_CLOSING_DATA_CONNECTION); // Closing data connection.
				exitFlag = 1;
			}},
//			{"LPRT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"LPSV",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"MDTM",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"MFCT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"MFF" ,[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"MFMT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"MIC" ,[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"MKD" ,[&](std::istringstream &ss){
				std::string DirName;
				ss >> DirName;
				DirName = std::string(cwd.path(WorkingDirectory::VFS)) + "/" + DirName;
				COUT << DirName << std::endl;
				if (mkdir(DirName.c_str(), 0666) == 0)
					sendResponse(FTPServer::RESPONSE_200_COMMAND_OK);
				else
					sendResponse(FTPServer::RESPONSE_550_ACTION_NOT_TAKEN);
			}},
//			{"MLSD",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"MLST",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"MODE",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"NLST",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"NOOP",[&](std::istringstream &ss){sendResponse(RESPONSE_200_COMMAND_OK);}},
//			{"OPTS",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"PASS",[&](std::istringstream &ss){
				std::string password;
				ss >> password;
				if (m_lastCommand != "USER") {
					sendResponse(RESPONSE_503_BAD_SEQUENCE);
					return;
				}
				if (m_userid == m_suppliedUserid && password == m_password) {// Compare the supplied userid and passwords.
					sendResponse(RESPONSE_230_USER_LOGGED_IN);
					m_isAuthenticated = true;
				} else {
					sendResponse(RESPONSE_530_NOT_LOGGED_IN);
					closeConnection();
					m_isAuthenticated = false;
				}
			}},
			{"PASV",[&](std::istringstream &ss){
				std::string ipInfo = listenPassive();
				std::ostringstream responseTextSS;
				responseTextSS << "Entering Passive Mode (" << ipInfo << ").";
				std::string responseText;
				responseText = responseTextSS.str();
				sendResponse(RESPONSE_227_ENTERING_PASSIVE_MODE, responseText.c_str());
				m_isPassive = true;
			}},
//			{"PBSZ",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"PORT",[&](std::istringstream &ss){
				char c;
				uint16_t h1, h2, h3, h4, p1, p2;
				ss >> h1 >> c >> h2 >> c >> h3 >> c >> h4 >> c >> p1 >> c >> p2;
				m_dataPort = p1 * 256 + p2;
				m_dataIp = h1 << 24 | h2 << 16 | h3 << 8 | h4;
				sendResponse(RESPONSE_200_COMMAND_OK); // Command okay.
				m_isPassive = false;
			}},
			{"PWD",[&](std::istringstream &ss){
				COUT<<"PWD Response to client: \""<<cwd.path(WorkingDirectory::FTP)<<"\""<<std::endl;
				sendResponse(257, "\"" + std::string(cwd.path(WorkingDirectory::FTP)) + "\"");
				exitFlag = 1;
			}},
			{"QUIT",[&](std::istringstream &ss){
				sendResponse(FTPServer::RESPONSE_221_CLOSING_CONTROL_CONNECTION); // Service closing control connection.
				closeConnection();  // Close the connection to the client.
			}},
//			{"REIN",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"REST",[&](std::istringstream &ss){ss >> this->restartMarker; sendResponse(RESPONSE_350_RESTART_MARKER_SET);}},
			{"RETR",[&](std::istringstream &ss){
				// We open a data connection back to the client.  We then invoke the callback to indicate that we have
				// started a retrieve operation.  We call the retrieve callback to request the next chunk of data and
				// transmit this down the data connection.  We repeat this until there is no more data to send at which
				// point we close the data connection and we are done.
				std::string fileName;
				ss >> fileName; 
				fileName = std::string(cwd.path(WorkingDirectory::VFS))+"/"+fileName;
				COUT<<"Retreiving: "<< fileName<<std::endl;
				uint8_t data[m_chunkSize];
				if (m_callbacks != nullptr) {
					try {
						m_callbacks->onRetrieveStart(fileName);
					} catch (FTPServer::FileException& e) {
						sendResponse(FTPServer::RESPONSE_550_ACTION_NOT_TAKEN);	  // Requested action not taken.
						return;
					}
				}
				sendResponse(FTPServer::RESPONSE_150_ABOUT_TO_OPEN_DATA_CONNECTION); // File status okay; about to open data connection.
				openData();
				if (m_callbacks != nullptr) {
					int readSize = m_callbacks->onRetrieveData(data, m_chunkSize);
					while (readSize > 0) {
						sendData(data, readSize);
						readSize = m_callbacks->onRetrieveData(data, m_chunkSize);
					}
				}
				closeData();
				sendResponse(FTPServer::RESPONSE_226_CLOSING_DATA_CONNECTION); // Closing data connection.
				if (m_callbacks != nullptr) {
					m_callbacks->onRetrieveEnd();
				}
			}},
			{"RMD",[&](std::istringstream &ss){	
				std::string DirName;
				ss >> DirName;
				DirName = cwd.VFShome() + DirName;
				COUT << DirName << std::endl;
				if (rmdir(DirName.c_str()) == 0){
					sendResponse(FTPServer::RESPONSE_200_COMMAND_OK);
					cwd.cupd();
				}
				else
					sendResponse(FTPServer::RESPONSE_550_ACTION_NOT_TAKEN);
			}},
			{"RMDIR",[&](std::istringstream &ss){
				cmdMap.at("RMD")(ss); // same as "RMD"
			}},
			{"RNFR",[&](std::istringstream &ss){
				ss >> m_renameFrom;
				m_renameFrom =  cwd.VFShome() + m_renameFrom;
				COUT<<"Renaming from "<<m_renameFrom<<std::endl;
				// Check that the file exists.
				if (access(m_renameFrom.c_str(), F_OK) != 0) {
					sendResponse(RESPONSE_550_ACTION_NOT_TAKEN); // Requested action not taken.
					m_renameFrom.clear();
					return;
				}
				sendResponse(RESPONSE_350_RESTART_MARKER_SET);
			}},// Rename From
			{"RNTO",[&](std::istringstream &ss){
				if (m_renameFrom.empty()) {
					sendResponse(RESPONSE_503_BAD_SEQUENCE); // Bad sequence of commands.
					return;
				}
				std::string renameTo;
				ss >> renameTo;
				renameTo = cwd.VFShome() + renameTo;
				COUT<<"Renaming "<<m_renameFrom<<" to "<<renameTo<<std::endl;
				if (rename(m_renameFrom.c_str(), renameTo.c_str()) != 0) {
					sendResponse(RESPONSE_550_ACTION_NOT_TAKEN); // Requested action not taken.
					m_renameFrom.clear();
					return;
				}
				m_renameFrom.clear();
				sendResponse(RESPONSE_250_FILE_ACTION_OK, "File renamed to " + renameTo + ".");
			}},// Rename To
//			{"SITE",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"SMNT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"STAT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"STOR",[&](std::istringstream &ss){
				std::string fileName;
				ss >> fileName;
				fileName = std::string(cwd.path(WorkingDirectory::VFS))+"/"+fileName;
				receiveFile(fileName);
			}},
//			{"STOU",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"SYST",[&](std::istringstream &ss){sendResponse(RESPONSE_215_UNIX_TYPE_L8);}},
			{"SYSTEM",[&](std::istringstream &ss){cmdMap.at("SYST")(ss);}},
//			{"TELE",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"TYPE",[&](std::istringstream &ss){
				std::string type;
				ss >> type;
				m_isImage = (type.compare("I") == 0);
				sendResponse(FTPServer::RESPONSE_200_COMMAND_OK);   // Command okay.
			}},
			{"USER",[&](std::istringstream &ss){
				// When we receive a user command, we next want to know if we should ask for a password.  If the m_loginRequired
				// flag is set then we do indeed want a password and will send the response that we wish one.
				std::string userName;
				ss >> userName;
				//cwd.v.clear();
				sendResponse(m_loginRequired ? FTPServer::RESPONSE_331_PASSWORD_REQUIRED : FTPServer::RESPONSE_200_COMMAND_OK);
				m_suppliedUserid = userName;   // Save the username that was supplied.
			}},
//			{"XCRC",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XMKD",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XRMD",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XPWD",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XRCP",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XSEN",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"SAU", [&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"SITE",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"SIZE",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"SMNT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"STAT",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"STOR",[&](std::istringstream &ss){
				std::string fileName;
				ss >> fileName;
				fileName = std::string(cwd.path(WorkingDirectory::VFS))+"/"+fileName;
				receiveFile(fileName);
			}},
//			{"STOU",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"SYST",[&](std::istringstream &ss){sendResponse(RESPONSE_215_UNIX_TYPE_L8);}},
			{"SYSTEM",[&](std::istringstream &ss){cmdMap.at("SYST")(ss);}},
//			{"TELE",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
			{"TYPE",[&](std::istringstream &ss){
				std::string type;
				ss >> type;
				m_isImage = (type.compare("I") == 0);
				sendResponse(FTPServer::RESPONSE_200_COMMAND_OK);   // Command okay.
			}},
			{"USER",[&](std::istringstream &ss){
				// When we receive a user command, we next want to know if we should ask for a password.  If the m_loginRequired
				// flag is set then we do indeed want a password and will send the response that we wish one.
				std::string userName;
				ss >> userName;
				//cwd.v.clear();
				sendResponse(m_loginRequired ? FTPServer::RESPONSE_331_PASSWORD_REQUIRED : FTPServer::RESPONSE_200_COMMAND_OK);
				m_suppliedUserid = userName;   // Save the username that was supplied.
			}},
//			{"XCRC",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XMKD",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XRMD",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XPWD",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XRCP",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
//			{"XSEN",[&](std::istringstream &ss){NOT_IMPLEMENTED}},
		}; 
};
#endif // NETWORKING_FTPSERVER_FTPSERVER_H_ 