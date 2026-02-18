#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include "utils/settings.hpp"
#include "utils/log.hpp"

size_t get_remaining_storage(const char *folder_path) {
    struct statvfs vfs;

    if (!folder_path || statvfs(folder_path, &vfs) != 0) {
        perror("get_remaining_storage: statvfs");
        return SIZE_MAX;  // error sentinel
    }

    // printf("%s: remaining space in %s is %zu bytes\n", __func__, folder_path, (size_t)vfs.f_bavail * (size_t)vfs.f_frsize);

    return (size_t)vfs.f_bavail * (size_t)vfs.f_frsize;
}


/**
 * This returns true if the disk is considered full.
 */
bool is_disk_full(){
    size_t remaining_size = get_remaining_storage(settings::DATA_DIR.c_str());

    if(remaining_size < settings::MINIMUM_REQUIRED_FS_BYTES){
        LOG_INFO("Remining storage in " + settings::DATA_DIR + " is : " + std::to_string(remaining_size/MB) + " MB");
        return true;
    }
    return false;
}