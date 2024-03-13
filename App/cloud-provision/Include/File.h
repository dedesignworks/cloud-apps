#ifndef FILE_H
#define FILE_H

#include <stddef.h>
#include <stdbool.h>

#define FILE_MAX_STRING_LENGTH 256

typedef struct sFileInfo {
    char filename[FILE_MAX_STRING_LENGTH];
    bool sendStatus;
} FileInfo;

int File_Validate(const char *file);
int File_Read(const char *file, char *data, size_t bufferSize);
int File_ReadList(const char *listFile, FileInfo *files, int maxFileCount);
int File_Delete(const char *file);
int File_CleanList(FileInfo *files, int count);
void FileInfo_SetSendStatus(FileInfo *fileInfo, bool status);

#endif