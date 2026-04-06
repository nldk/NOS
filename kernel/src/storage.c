#include "storage.h"
#include "utils.h"
#include "vga.h"
#include "mem.h"

#define ATA_TIMEOUT 1000000U

int ata_wait_busy() {
    for (unsigned int i = 0; i < ATA_TIMEOUT; i++) {
        unsigned char status = inb(0x1F7);
        if ((status & ATA_SR_BSY) == 0) {
            if (status & (ATA_SR_ERR | ATA_SR_DF)) {
                return 0;
            }
            return 1;
        }
    }
    return 0;
}

int ata_wait_drq() {
    for (unsigned int i = 0; i < ATA_TIMEOUT; i++) {
        unsigned char status = inb(0x1F7);

        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return 0;
        }

        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ)) {
            return 1;
        }
    }
    return 0;
}

int ata_read_sector(unsigned int lba, unsigned char *buffer){
    if (!ata_wait_busy()) {
        return 0;
    }

    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb( 0x1F2, 1);
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);

    outb(0x1F7, 0x20);

    if (!ata_wait_drq()) {
        return 0;
    }

    for (int i = 0; i < 256;i++){
        unsigned short word = inw(0x1F0);
        unsigned char low = (unsigned char)(word & 0xFF);
        unsigned char high = (unsigned char)(word >> 8);
        buffer[i*2] = low;
        buffer[i*2+1] = high;
    }

    return 1;
}

int ata_write_sector(unsigned int lba, unsigned char *buffer){
    if (!ata_wait_busy()) {
        return 0;
    }

    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F)); // master + LBA
    outb(0x1F2, 1); // sector count
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);

    outb(0x1F7,0x30);

    if (!ata_wait_drq()) {
        return 0;
    }

    for(int i = 0; i < 256; i++){
        unsigned short word = (unsigned short)buffer[i * 2] |
                              ((unsigned short)buffer[i * 2 + 1] << 8);
        outw(0x1F0, word);
    }

    outb(0x1F7, 0xE7);
    if (!ata_wait_busy()) {
        return 0;
    }

    return 1;
}

int ata_read_sectors(unsigned int lba, unsigned char *buffer, unsigned int count){
    if (!ata_wait_busy()) {
        return 0;
    }

    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb( 0x1F2, count);
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);

    outb(0x1F7, 0x20);

    if (!ata_wait_drq()) {
        return 0;
    }

    for (int i = 0; i < count * 256;i++){
        unsigned short word = inw(0x1F0);
        unsigned char low = (unsigned char)(word & 0xFF);
        unsigned char high = (unsigned char)(word >> 8);
        buffer[i*2] = low;
        buffer[i*2+1] = high;
    }

    return 1;
}
int ata_write_sectors(unsigned int lba, unsigned char *buffer, unsigned int count){
    if (!ata_wait_busy()) {
        return 0;
    }

    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F)); // master + LBA
    outb(0x1F2, count); // sector count
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);

    outb(0x1F7,0x30);

    if (!ata_wait_drq()) {
        return 0;
    }

    for(int i = 0; i < 256 * count; i++){
        unsigned short word = (unsigned short)buffer[i * 2] |
                              ((unsigned short)buffer[i * 2 + 1] << 8);
        outw(0x1F0, word);
    }

    outb(0x1F7, 0xE7);
    if (!ata_wait_busy()) {
        return 0;
    }

    return 1;
}

FileSystem* createFileSystem(unsigned int startLBA){
    FileSystem* fs = malloc(sizeof(FileSystem));
    Directory* root = createRootDirectory("root");
    fs->rootDirectory = root;
    fs->startLBA = startLBA;
    return fs;
}

FileSystem* readFileSystem(unsigned int startLBA){
    printf("Reading fs from disk noet implemented!!!\n");
}

File* createFile(const char* name, unsigned int size, Directory* parent){
    File* file = malloc(sizeof(File));
    file->name = malloc(str_len(name) + 1);
    str_cp(name,file->name);
    file->data = malloc(size);
    if (size <= 0){
        file->size = 1;
    }else{      
        file->size = size;
    }
    file->type = FILE_TYPE_FILE;
    parent->files[findNextAvailableFile(parent)] = file;
    return file;
}
 
char* readFile(unsigned int* bufferSize,File* file){
    if (!file)
    {
        *bufferSize = 0;
        return "";
    }
    
    *bufferSize = file->size;
    return file->data;
}

void deleteFile(File* file){
    free(file->data);
    file->data = 0;
    free(file->name);
    file->name = 0;
    free(file);
    file = 0;
}

int writeFile(const unsigned char* data, unsigned int dataSize, File* file){
    if(file->size< dataSize){
        free(file->data);
        file->size = dataSize;
        file->data = malloc(dataSize);
    }
    cp_buff(data,file->data,dataSize);
    return 0;
}

int listDirectorie(Directory* directory, Directory** directories, File** files, int maxDirs, int maxFiles){
    for (int i = 0; i < directory->amountOfMaxFiles; i++){
        if (maxFiles<=i){
            break;
        }
        if(directory->files[i]){
            files[i]=directory->files[i];
        }
    }
    for (int i = 0; i < directory->amountOfMaxDirectories; i++){
        if (maxDirs<i){
            break;
        }
        if(directory->directories[i]){
            directories[i]=directory->directories[i];
        }
    }
    return 0;
}

int deleteDirectory(Directory* directory){
    for (int i = 0; i < directory->amountOfMaxFiles; i++){
        if (directory->files[i]){
            deleteFile(directory->files[i]);
        }
    }
    for (int i = 0; i < directory->amountOfMaxDirectories; i++){
        if (directory->directories[i]){
            deleteDirectory(directory->directories[i]);
        }
    }
    free(directory->directories);
    free(directory->files);
    free(directory->name);
    free(directory);
    return 0;
}

Directory* createDirectory(const char* name, Directory* parent){
    Directory* dir = malloc(sizeof(Directory));
    dir->amountOfMaxDirectories=INITIALDIRCAP;
    dir->amountOfMaxFiles=INITIALDIRCAP;
    dir->directories=malloc(INITIALDIRCAP*sizeof(Directory*));
    dir->files=malloc(INITIALDIRCAP*sizeof(File*));
    dir->name = malloc(str_len(name) + 1);
    str_cp(name,dir->name);
    dir->type = FILE_TYPE_DIRECTORY;
    for (int i = 0; i < INITIALDIRCAP; i++){
        dir->files[i] = 0;
        dir->directories[i] = 0;
    }
    parent->directories[findNextAvailableDir(parent)] = dir;
    return dir;
}
Directory* createRootDirectory(const char* name){
    Directory* dir = malloc(sizeof(Directory));
    dir->amountOfMaxDirectories=INITIALDIRCAP;
    dir->amountOfMaxFiles=INITIALDIRCAP;
    dir->directories=malloc(INITIALDIRCAP*sizeof(Directory*));
    dir->files=malloc(INITIALDIRCAP*sizeof(File*));
    dir->name = malloc(str_len(name) + 1);
    str_cp(name,dir->name);
    for (int i = 0; i < INITIALDIRCAP; i++){
        dir->files[i] = 0;
        dir->directories[i] = 0;
    }
    dir->type = FILE_TYPE_DIRECTORY;
    return dir;
}
int findNextAvailableFile(Directory* dir){
    File** files = dir->files;
    for (int i = 0; i < dir->amountOfMaxFiles; i++){
        if (files[i]==0){
            return i;
        }
    }
    File** tmpFiles = malloc((dir->amountOfMaxFiles*2)*sizeof(File*));
    int index = 0;
    for (int i = 0; i < dir->amountOfMaxFiles; i++){
        tmpFiles[index] = files[index];
        index++;
    }
    int next = index;
    dir->amountOfMaxFiles *=2;
    free(dir->files);
    dir->files = tmpFiles;
    for (int i = index; i < dir->amountOfMaxFiles; i++){
        dir->files[i]=0;
    }
    return next;
}
int findNextAvailableDir(Directory* dir){
    Directory** dirs = dir->directories;
    for (int i = 0; i < dir->amountOfMaxDirectories; i++){
        if (dirs[i]==0){
            return i;
        }
    }
    Directory** tmpDirs = malloc((dir->amountOfMaxDirectories*2)*sizeof(Directory*));
    int index = 0;
    for (int i = 0; i < dir->amountOfMaxDirectories; i++){
        tmpDirs[index] = dirs[index];
        index++;
    }
    int next = index;
    dir->amountOfMaxDirectories *=2;
    free(dir->directories);
    dir->directories = tmpDirs;
    for (int i = index; i < dir->amountOfMaxDirectories; i++){
        dir->directories[i]=0;
    }
    return next;
}

int formatDisk(unsigned int startLba){
    SuperBlock sp = {0xCADE,0,startLba,0};
    DiskDir dd = {"root",0,0,0};
    char buff[512] = {0};
    memcpy(buff, &sp, sizeof(SuperBlock));
    memcpy(buff + sizeof(SuperBlock), &dd, sizeof(DiskDir));

    ata_write_sector(startLba,buff);
}

int loadFs(unsigned int startLba){
    char buff[512] = {0};
    ata_read_sector(startLba,buff);
    SuperBlock sp;
    DiskDir rd;
    memcpy(&sp,buff,sizeof(SuperBlock));
    memcpy(&rd,buff+sizeof(SuperBlock),sizeof(DiskDir));
}

DiskDir loadDirFromDisk(unsigned long long addr){
    unsigned int offset = 0;
    unsigned long long lba = addrToLba(&offset,addr);
    DiskDir dd;
    if (offset>512-sizeof(DiskDir)){
        char buff1[512];
        ata_read_sector(lba,buff1);
        char buff2[512];
        ata_read_sector(lba+1,buff2);
        memcpy(&dd,buff1+offset,512-offset);
        memcpy(&dd+(512-offset),buff2,sizeof(DiskDir)-(512-offset));
    }else{
        char buff[512];
        ata_read_sector(lba,buff);
        memcpy(&dd,buff+offset,sizeof(DiskDir));
    }
    return dd;
}

Directory* loadDir(unsigned long long addr){
    DiskDir dd = loadDirFromDisk(addr);
    Directory* dir = createRootDirectory(dd.name);
    dir->addr = addr;
    unsigned long long currentDirAddr = dd.dirTable;

    while (currentDirAddr != 0) {
        DiskDir childDiskDir = loadDirFromDisk(currentDirAddr);
        Directory* childDir = loadDir(currentDirAddr); // recursion
        dir->directories[findNextAvailableDir(dir)] = childDir;
        currentDirAddr = childDiskDir.nextDir;
    }

    unsigned long long currentFileAddr = dd.fileTable;

    while (currentFileAddr != 0) {
        DiskFile df;

        unsigned int offset = 0;
        unsigned long long lba = addrToLba(&offset, currentFileAddr);

        char buff[512];
        ata_read_sector(lba, buff);

        memcpy(&df, buff + offset, sizeof(DiskFile));

        File* file = malloc(sizeof(File));
        file->type = FILE_TYPE_FILE;

        file->name = malloc(33);
        str_cp(df.name, file->name);

        file->size = df.size;
        file->data = 0;
        file->firstDataAddr = df.firstDataLBA;
        file->addr = currentFileAddr;

        dir->files[findNextAvailableFile(dir)] = file;
        
        currentFileAddr = df.nextFile;
    }
    
}

char* loadFileDataFromDisk(File* file,unsigned int size){
    char* data;
    if (size){
        data = malloc(file->size);
        size = file->size;
    }else{
        data = malloc(size);
    }
    unsigned long long addr = file->firstDataAddr;

    
    
}

char* readDataBlock(unsigned long long addr,unsigned long long* nextAddr){
    char buffS[512];
    unsigned int offset=0;
    ata_read_sector(addrToLba(&offset,addr),&buffS);
    int size = 0;
    memcpy(&buffS+offset,&size,sizeof(int));
    int c = ceil(size/512);
    char* dataBuff = malloc(c*512);
    ata_read_sectors(addrToLba(&offset,addr+sizeof(int)),dataBuff,ceil(size/512));
    //memcpy();
    return dataBuff;
}

unsigned long long addrToLba(unsigned int* offset,unsigned long long addr){
    unsigned long long lba = floor(addr/512);
    *offset = addr - lba * 512;
    return lba;
}