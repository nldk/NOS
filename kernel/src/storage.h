#ifndef STORAGE_H
#define STORAGE_H

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01
#define ATA_SR_DF   0x20
#define FileSystemStart 10
#define INITIALDIRCAP 10

typedef struct Directory Directory;

enum FileType{
    FILE_TYPE_FILE,
    FILE_TYPE_DIRECTORY
};
typedef struct{
    enum FileType type;
    char* name;
    unsigned int size;
    char* data;
    unsigned long long firstDataAddr;
    unsigned long long addr;
}File;
struct Directory{
    enum FileType type;
    char* name;
    Directory** directories;
    int amountOfMaxDirectories;
    File** files;
    int amountOfMaxFiles;
    unsigned long long addr;
};
typedef struct {
    unsigned int startLBA;
    Directory* rootDirectory;
} FileSystem;

FileSystem* createFileSystem(unsigned int startLBA);

FileSystem* readFileSystem(unsigned int startLBA);

File* createFile(const char* name, unsigned int size, Directory* parent);
 
char* readFile(unsigned int* bufferSize,File* file);

void deleteFile(File* file);

int writeFile(const unsigned char* data, unsigned int dataSize, File* file);

int listDirectorie(Directory* directory, Directory** directories, File** files, int maxDirs, int maxFiles);

int deleteDirectory(Directory* directory);

Directory* createDirectory(const char* name, Directory* parent);

int findNextAvailableDir(Directory* dir);

int findNextAvailableFile(Directory* dir);

Directory* createRootDirectory(const char* name);

int ata_wait_busy();

int ata_wait_drq();

int ata_read_sector(unsigned int lba, unsigned char *buffer);

int ata_write_sector(unsigned int lba, unsigned char *buffer);

int ata_read_sectors(unsigned int lba, unsigned char *buffer, unsigned int count);

int ata_write_sectors(unsigned int lba, unsigned char *buffer, unsigned int count);

int ata_identify(unsigned short *identify_words);

int ata_smoke_test(void);


typedef struct {
    unsigned short magicNumber;
    unsigned int version;
    unsigned int startLBA;
    unsigned int sizeInSectors;
}SuperBlock;

typedef struct {
    char name[32];
    unsigned long long dirTable;
    unsigned long long fileTable;
    unsigned long long nextDir;
    char used;
}DiskDir;
typedef struct {
    char name[32];
    unsigned int size;
    unsigned int firstDataLBA;
    unsigned long long nextFile;
    char used;
} DiskFile;

int formatDisk(unsigned int startLba);

int loadFs(unsigned int startLba);

unsigned long long addrToLba(unsigned int* offset,unsigned long long addr);
#endif