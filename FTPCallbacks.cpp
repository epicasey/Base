/**
 * FTPCallbacks.cpp
 *
 *  Created on: May 20, 2017
 *  Ported to ESP32 on 2025-10-14
 */
#include "FTPServer.h"
#include <stdint.h>
#include <fstream>
#include <dirent.h>
#include <esp_log.h>
#include <iostream>
//#include "Common.h"
#include "base.h"

/**
 * Called at the start of a STOR request.  The file name is the name of the file the client would like to
 * save.
 */
void FTPFileCallbacks::onStoreStart(std::string fileName) {
	LOGD(">> FTPFileCallbacks::onStoreStart: fileName=%s", fileName.c_str());
	m_storeFile.open(fileName, std::ios::binary);                        // Open the file for writing.
	if (m_storeFile.fail()) {
    LOGE("Failed to open file: %s, errno=%d (%s)", fileName.c_str(), errno, strerror(errno));
    throw FTPServer::FileException(fileName, errno, strerror(errno));
}
	LOGD("<< FTPFileCallbacks::onStoreStart");
} // FTPFileCallbacks#onStoreStart


/**
 * Called when the client presents a new chunk of data to be saved.
 */
size_t FTPFileCallbacks::onStoreData(uint8_t* data, size_t size) {
	LOGD(">> FTPFileCallbacks::onStoreData: size=%d", size);
	m_storeFile.write((char*) data, size);							   // Store data received.
	LOGD("<< FTPFileCallbacks::onStoreData: size=%d", size);
	return size;
} // FTPFileCallbacks#onStoreData


/**
 * Called at the end of a STOR request.  This indicates that the client has completed its transmission of the
 * file.
 */
void FTPFileCallbacks::onStoreEnd() {
	LOGD(">> FTPFileCallbacks::onStoreEnd");
	m_storeFile.close();                                                 // Close the open file.
	LOGD("<< FTPFileCallbacks::onStoreEnd");
} // FTPFileCallbacks#onStoreEnd


/**
 * Called when the client requests retrieval of a file.
 */
void FTPFileCallbacks::onRetrieveStart(std::string fileName) {
	LOGD(">> FTPFileCallbacks::onRetrieveStart: fileName=%s", fileName.c_str());
	m_byteCount = 0;
	m_retrieveFile.open(fileName, std::ios::binary);
	if (m_retrieveFile.fail()) {
		LOGW("<< FTPFileCallbacks::onRetrieveStart: ***FileException***");
		throw FTPServer::FileException();
	}
	LOGD("<< FTPFileCallbacks::onRetrieveStart");
} // FTPFileCallbacks#onRetrieveStart


/**
 * Called when the client is ready to receive the next piece of the file.  To indicate that there
 * is no more data to send, return a size of 0.
 * @param data The data buffer that we can fill to return data back to the client.
 * @param size The maximum size of the data buffer that we can populate.
 * @return The size of data being returned.  Return 0 to indicate that there is no more data to return.
 */
size_t FTPFileCallbacks::onRetrieveData(uint8_t* data, size_t size) {
	LOGD(">> FTPFileCallbacks::onRetrieveData");
	m_retrieveFile.read((char*) data, size);
	size_t readSize = m_retrieveFile.gcount();
	m_byteCount += readSize;
	LOGD("<< FTPFileCallbacks::onRetrieveData: sizeRead=%d", readSize);
	return m_retrieveFile.gcount();  // Return the number of bytes read.
} // FTPFileCallbacks#onRetrieveData


/**
 * Called when the retrieval has been completed.
 */
void FTPFileCallbacks::onRetrieveEnd() {
	LOGD(">> FTPFileCallbacks::onRetrieveEnd");
	m_retrieveFile.close();
	LOGD("<< FTPFileCallbacks::onRetrieveEnd: bytesTransmitted=%ld", m_byteCount);
} // FTPFileCallbacks#onRetrieveEnd

/**
 * Return a list of files in the file system.
 * @return a list of files in the file system.
 */
std::string FTPFileCallbacks::onDir() {
	COUT<<"From "<<FTPServer::cwd.path(WorkingDirectory::VFS)<<std::endl;
	DIR* dir = opendir(FTPServer::cwd.path(WorkingDirectory::VFS));
	if(dir==nullptr){COUT<<"Error opening directory"<<std::endl; return "";}
	std::stringstream ss;
	int n=0;
	while (true) {
		struct dirent* pDirentry = readdir(dir);
		if (pDirentry == nullptr) {COUT<<"Directory ends after "<<n-1<<" files."<<std::endl;break;}
		struct stat st;
		n++;
		std::string s = FTPServer::cwd.path(WorkingDirectory::VFS); s+="/"+std::string(pDirentry->d_name);
		stat(s.c_str(), &st);
		struct tm tx;
		localtime_r(&st.st_mtime, &tx);
		ss << (pDirentry->d_type ==DT_DIR?"d":"-")<<"rw-rw-rw- 1 none "<<st.st_size<<" "<<(1900+tx.tm_year)<<"-"<<1+tx.tm_mon<<"-"<<tx.tm_mday<<" "<<tx.tm_hour<<":"<<tx.tm_min<<" "<<pDirentry->d_name << "\r\n";
	}
	closedir(dir);
	return ss.str();
} // FTPFileCallbacks#onDir


/// ---- END OF FTPFileCallbacks


