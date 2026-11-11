/*
 * FATFS.h
 *
 *  Created on: May 20, 2017
 *  Ported to ESP32 on 2025-10-14
 *      Author: kolban
 */

 
#ifndef COMPONENTS_CPP_UTILS_FATFS_VFS_H_
#define COMPONENTS_CPP_UTILS_FATFS_VFS_H_

#include <string>
#include <memory>
extern "C" {
#include <esp_vfs_fat.h>
#include <wear_levelling.h>
}
//#include "Common.h"
#include "base.h"

/**
 * @brief Provide access to the FAT file system on a sdCard.
 * @code{.cpp}
 * auto fs = FATFS_VFS::create("/spiflash");
 * fs->mount();
 * // Perform file I/O
 * fs->unmount();
 * @endcode
 *
 */
class FATFS_VFS : public std::enable_shared_from_this<FATFS_VFS> {
public:
	static std::shared_ptr<FATFS_VFS> create(std::string mountPath = MOUNT_POINT);
	FATFS_VFS(std::string mountPath = MOUNT_POINT);
	virtual ~FATFS_VFS();
	const std::string& mount();
	void setMaxFiles(int maxFiles);
	void unmount();
	void startTask();
	static int64_t GetSize(void);
	static bool isMounted() { return ismounted; }
private:
	static bool ismounted;
    sdmmc_card_t *m_wl_handle; //card
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	std::string m_mountPath {MOUNT_POINT};
	int         m_maxFiles  {5};
    spi_bus_config_t bus_cfg = {};
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	static void Task(void * pd); };
#endif /* COMPONENTS_CPP_UTILS_FATFS_VFS_H_ */