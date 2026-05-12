#ifndef STORAGE_H
#define STORAGE_H

typedef struct {
    unsigned int inode;
    unsigned char type;
    char name[256];
}Ext2DirEntry;

#endif