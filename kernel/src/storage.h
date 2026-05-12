#ifndef STORAGE_H
#define STORAGE_H

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01
#define ATA_SR_DF   0x20

#include "utils.h"

typedef struct{
    unsigned long long addr;
    unsigned long long data;
}File;

typedef struct {
    unsigned long long addr;
    unsigned long long* dirTable;
}Dir;

typedef struct {
    unsigned int inode;
    unsigned char type;
    char name[256];
}Ext2DirEntry;

#define EXT2_ERR_NONE 0
#define EXT2_ERR_NOT_MOUNTED 1
#define EXT2_ERR_NOT_FOUND 2
#define EXT2_ERR_EXISTS 3
#define EXT2_ERR_NOT_DIR 4
#define EXT2_ERR_NOT_FILE 5
#define EXT2_ERR_NO_SPACE 6
#define EXT2_ERR_IO 7
#define EXT2_ERR_INVALID 8

int ata_wait_busy();
int ata_wait_drq();
int ata_read_sector(unsigned int lba, unsigned char *buffer);
int ata_write_sector(unsigned int lba, unsigned char *buffer);
int ata_read_sectors(unsigned int lba, unsigned char *buffer, unsigned int count);
int ata_write_sectors(unsigned int lba, unsigned char *buffer, unsigned int count);
int ata_identify(unsigned short *identify_words);
int ata_smoke_test(void);
unsigned int addrToLBA(unsigned long long addr, int* offset);
void readBytes(unsigned long long addr,unsigned int bytes, char* buffer);
void writeBytes(unsigned long long addr, unsigned int bytes, char* buff);

int ext2_mount(unsigned int start_lba);
int ext2_read_file(const char* path, unsigned char* buffer, unsigned int max_bytes, unsigned int* out_size);
int ext2_read_dir(const char* path, Ext2DirEntry** entries_out, unsigned int* out_count);
int ext2_write_file_overwrite(const char* path, const unsigned char* data, unsigned int size);
int ext2_create_file(const char* path, const unsigned char* data, unsigned int size);
int ext2_mkdir(const char* path);
int ext2_delete(const char* path);
int ext2_delete_recursive(const char* path);
int ext2_last_error(void);


#endif