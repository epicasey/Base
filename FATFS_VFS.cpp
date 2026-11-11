/*
 * FATFS.cpp
 *
 *  Created on: May 20, 2017
 *  Ported to ESP32 on 2025-10-14
 *      Author: kolban
 */

#include "FATFS_VFS.h"
#include "FTPServer.h"
#include <esp_err.h>

#include <esp_vfs_fat.h>
#include "sdmmc_cmd.h"

bool FATFS_VFS::ismounted = false;

/**
 * @brief Constructor.
 *
 * In ESP32, every file system has a mount point.  If a file access is attempted south of that
 * mount point, then the corresponding file system will be used.  The `mountPath` parameter defines
 * the mount point for this instance of the FATFS file system.
 * In order to save the files for subsequent retrieval, the file data has to be written to flash memory.
 * A partition table provides a map or layout of the flash memory by defining named partitions.  The
 * `partitionName` parameter defines the name of the partition used to provide the underlying storage
 * for this instance of the FATFS file system.
 *
 * @param [in] mountPath The path in the VFS where the FAT file system should be mounted.
 * @param [in] partitionName The name of the partition used to store the FAT file system.
 */
std::shared_ptr<FATFS_VFS> FATFS_VFS::create(std::string mountPath) {
    auto instance = std::shared_ptr<FATFS_VFS>(new FATFS_VFS(std::move(mountPath)));
    instance->startTask();
    return instance;
}

FATFS_VFS::FATFS_VFS(std::string mountPath) {
	m_mountPath     = std::move(mountPath);
	m_maxFiles      = 5;
} // FATFS_VFS

FATFS_VFS::~FATFS_VFS() {
} // ~FATFS_VFS

/**
 * @brief Mount the FAT file system into VFS.
 * The FAT file system found in the partition is mounted into the VFS.
 * @return N/A.
 */
const std::string& FATFS_VFS::mount() {
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = true;
    mount_config.max_files = m_maxFiles;
    mount_config.allocation_unit_size = 16 * 1024;
    LOGD("Initializing SD card");

    LOGD("Using SPI peripheral");
    bus_cfg.mosi_io_num   = PIN_NUM_MOSI;
    bus_cfg.miso_io_num   = PIN_NUM_MISO;
    bus_cfg.sclk_io_num   = PIN_NUM_CLK;
    bus_cfg.quadwp_io_num = PIN_NUM_WP;
    bus_cfg.quadhd_io_num = PIN_NUM_CD;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;
    ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        LOGE("Failed to initialize bus.");
        ismounted = false;
        return m_mountPath;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = (spi_host_device_t) host.slot;

    LOGD("Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(m_mountPath.c_str(), &host, &slot_config, &mount_config, &m_wl_handle);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            LOGE("Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            LOGE("Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        ismounted = false;
        return m_mountPath;
    }
    LOGI("Filesystem mounted");
        // Card has been initialized, print its properties
    //sdmmc_card_print_info(stdout, m_wl_handle);
    GetSize();
    ismounted = true;
    return m_mountPath;
} // mount

int64_t FATFS_VFS::GetSize(void){ FATFS *fs;
	DWORD fre_clust,
//          fre_sect, 
          tot_sect;

	/* Get volume information and free clusters of sdcard */
	auto res = f_getfree("/sdcard/", &fre_clust, &fs);
	if (res) return -1;

	/* Get total sectors and free sectors */
	tot_sect = (fs->n_fatent - 2) * fs->csize;
//	fre_sect = fre_clust * fs->csize;

	
	uint64_t tmp_total_bytes = (uint64_t)tot_sect * FF_SS_SDCARD;
//	uint64_t tmp_free_bytes = (uint64_t)fre_sect * FF_SS_SDCARD;

	/* Print the free space in bytes */
//    COUT<<tmp_total_bytes<<" total bytes. "<<tmp_free_bytes<<" free bytes. sector_size="<<FF_SS_SDCARD<<endl;
//	print("%llu total bytes. %llu free bytes. sector_size=%u", tmp_total_bytes, tmp_free_bytes, FF_SS_SDCARD);}
    return tmp_total_bytes;
}

/**
 * @brief Set the allowable number of concurrently open files.
 * @param [in] maxFiles Number of concurrently open files.
 * @return N/A.
 */
void FATFS_VFS::setMaxFiles(int maxFiles) {
	m_maxFiles = maxFiles;
} // setMaxFiles


/**
 * @brief Unmount a previously mounted file system.
 * @return N/A.
 */
void FATFS_VFS::unmount() {
    ESP_ERROR_CHECK(esp_vfs_fat_sdcard_unmount(m_mountPath.c_str(), m_wl_handle));
    spi_bus_free((spi_host_device_t)host.slot);
    LOGI("Filesystem unmounted");
    ismounted = false;
} // unmount

void FATFS_VFS::startTask() {
    auto taskSelf = new std::shared_ptr<FATFS_VFS>(shared_from_this());
    xTaskCreate(&Task, "File_System", 8192, taskSelf, 5, NULL);
}

void FATFS_VFS::Task(void * pd){
    auto taskSelf = static_cast<std::shared_ptr<FATFS_VFS>*>(pd);
    std::shared_ptr<FATFS_VFS> fs = *taskSelf;
    delete taskSelf;

    LOGI("FTPServer starting!\n");
    FTPServer ftpServer(fs->mount().c_str());
    ftpServer.setCallbacks(std::make_unique<FTPFileCallbacks>());
	ftpServer.start(); // task loop is inside here
    fs->unmount();
    vTaskDelete(NULL);
}