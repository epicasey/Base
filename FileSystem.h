/*
 * FileSystem.h
 *
 *  Created on: May 20, 2017
 *      Author: kolban
 */

#ifndef COMPONENTS_CPP_UTILS_FILESYSTEM_H_
#define COMPONENTS_CPP_UTILS_FILESYSTEM_H_
#include <string>
#include <vector>
/**
 * @brief File system utilities.
 */
typedef struct {std::string path; uint8_t type;} File;
class FileSystem {
	
public:
	static std::vector<File>        getDirectoryContents(const std::string &&path);
	static void                     dumpDirectory(       const std::string &&path);
	static bool                     isDirectory(         const std::string &&path);
	static int                      mkdir(               const std::string &&path);
	static std::vector<std::string> pathSplit(           const std::string &&path);
	static int                      remove(              const std::string &&path);
};

#endif /* COMPONENTS_CPP_UTILS_FILESYSTEM_H_ */