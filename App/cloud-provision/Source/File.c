#include "File.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int File_Validate(const char *file)
{
    return access(file, F_OK);
}

int File_Read(const char *file, char *data, size_t bufferSize)
{
    if (file == NULL || data == NULL || bufferSize == 0) {
        return -1;
    }

    /* Open the file in read mode */
    FILE *fptr = fopen(file, "r");

    if (fptr == NULL) {
        return -1;
    }

    int ch;
    size_t n = 0;

    memset(data, 0, bufferSize);

    while ((ch = fgetc(fptr)) != EOF) {
        data[n] = (char)ch;
        if (++n == bufferSize - 1) {
            break;
        }
    }

    /* Add null terminator at the end, just in case the buffer is dirty. */
    data[n] = '\0';
    fclose(fptr);

    return 0;
}

int File_ReadList(const char *listFile, FileInfo *files, int maxFileCount)
{
    FILE *fptr = fopen(listFile, "r");
    char file[FILE_MAX_STRING_LENGTH];
    int fileCount = 0;

    while (fgets(file, sizeof(file), fptr)) {
        size_t len = strlen(file);

        if (file[len - 1] == '\n') {
            file[len - 1] = '\0';
        }

        strncpy(files[fileCount].filename, file, FILE_MAX_STRING_LENGTH - 1);
        files[fileCount].sendStatus = false;
        fileCount++;

        if (fileCount == maxFileCount) {
            break;
        }
    }

    fclose(fptr);
    return fileCount;
}

int File_Delete(const char *file)
{
    return remove(file);
}

int File_CleanList(FileInfo *files, int count)
{
    for (size_t i = 0; i < count; i++) {
        if (files[i].sendStatus) {
            /* Delete file */
            char *fn = files[i].filename;
            if (File_Delete(fn)) {
                printf("Failed to delete %s\n", fn);
            }
        }
    }

    return 0;
}

void FileInfo_SetSendStatus(FileInfo *fileInfo, bool status)
{
    if (fileInfo) {
        fileInfo->sendStatus = true;
    }
}