
#include <malloc.h>
#include <string.h>
#include <resources/Resources.h>
#include <resources/filelist.h>
#include <system/AsyncDeleter.h>
#include <fs/FSUtils.h>
#include "gui/gui.h"

Resources * Resources::instance = NULL;

void Resources::Clear() {
    ResourceFile * ResourceList = getResourceList();
    if(ResourceList == NULL) return;

    for(int32_t i = 0; ResourceList[i].filename != NULL; ++i) {
        if(ResourceList[i].CustomFile) {
            free(ResourceList[i].CustomFile);
            ResourceList[i].CustomFile = NULL;
        }

        if(ResourceList[i].CustomFileSize != 0)
            ResourceList[i].CustomFileSize = 0;
    }

    if(instance)
        delete instance;

    instance = NULL;
}

bool Resources::LoadFiles(const char * path) {
    if(!path)
        return false;

    bool result = false;
    Clear();

    ResourceFile * ResourceList = getResourceList();
    if(ResourceList == NULL) return false;

    for(int32_t i = 0; ResourceList[i].filename != NULL; ++i) {
        std::string fullpath(path);
        fullpath += "/";
        fullpath += ResourceList[i].filename;

        uint8_t * buffer = NULL;
        uint32_t filesize = 0;

        FSUtils::LoadFileToMem(fullpath.c_str(), &buffer, &filesize);

        ResourceList[i].CustomFile = buffer;
        ResourceList[i].CustomFileSize = (uint32_t) filesize;
        result |= (buffer != 0);
    }

    return result;
}

const uint8_t * Resources::GetFile(const char * filename) {
    ResourceFile * ResourceList = getResourceList();
    if(ResourceList == NULL) return NULL;

    for(int32_t i = 0; ResourceList[i].filename != NULL; ++i) {
        if(strcasecmp(filename, ResourceList[i].filename) == 0) {
            return (ResourceList[i].CustomFile ? ResourceList[i].CustomFile : ResourceList[i].DefaultFile);
        }
    }

    return NULL;
}

uint32_t Resources::GetFileSize(const char * filename) {
    ResourceFile * ResourceList = getResourceList();
    if(ResourceList == NULL) return 0;

    for(int32_t i = 0; ResourceList[i].filename != NULL; ++i) {
        if(strcasecmp(filename, ResourceList[i].filename) == 0) {
            return (ResourceList[i].CustomFile ? ResourceList[i].CustomFileSize : ResourceList[i].DefaultFileSize);
        }
    }
    return 0;
}
