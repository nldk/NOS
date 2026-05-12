#include "storage.h"
#include "mem.h"
#include "utils.h"

#define EXT2_SUPERBLOCK_OFFSET 1024
#define EXT2_SUPER_MAGIC 0xEF53

#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2

#define EXT2_FEATURE_INCOMPAT_64BIT 0x00000080

#define EXT2_BLOCK_BUF_MAX 4096

#define EXT2_DEBUG 1

#if EXT2_DEBUG
static void dbg_puts(const char* msg) {
    serial_write_string(msg);
}

static void dbg_u32(const char* label, unsigned int value) {
    char* str = int_to_str((int)value);
    if (!str) {
        return;
    }
    serial_write_string(label);
    serial_write_string(str);
    serial_write_string("\r\n");
    free(str);
}
#else
static void dbg_puts(const char* msg) { (void)msg; }
static void dbg_u32(const char* label, unsigned int value) { (void)label; (void)value; }
#endif

typedef struct {
    unsigned int block_size;
    unsigned int block_sectors;
    unsigned int inode_size;
    unsigned int blocks_per_group;
    unsigned int inodes_per_group;
    unsigned int first_data_block;
    unsigned int blocks_count;
    unsigned int inodes_count;
    unsigned int groups_count;
    unsigned int first_inode;
    unsigned int group_desc_size;
    unsigned int features_incompat;
    unsigned long long fs_start_lba;
    unsigned long long fs_start_addr;
    unsigned long long gdt_start_block;
    char mounted;
} Ext2Fs;

static Ext2Fs g_ext2;
static unsigned char g_ext2_data_buf[EXT2_BLOCK_BUF_MAX];
static unsigned char g_ext2_index_buf[EXT2_BLOCK_BUF_MAX];
static int g_ext2_last_error = EXT2_ERR_NONE;

static int ext2_set_error(int code) {
    g_ext2_last_error = code;
    return 0;
}

static void ext2_dbg_last_error(const char* where) {
    if (!where) {
        where = "ext2";
    }
    serial_write_string(where);
    serial_write_string(": last_error=");
    char* err = int_to_str(g_ext2_last_error);
    if (err) {
        serial_write_string(err);
        free(err);
    }
    serial_write_string("\r\n");
}

int ext2_last_error(void) {
    return g_ext2_last_error;
}

static unsigned short rd16(const unsigned char* p) {
    return (unsigned short)p[0] | (unsigned short)(p[1] << 8);
}

static unsigned int rd32(const unsigned char* p) {
    return (unsigned int)p[0] |
           ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

static unsigned long long rd64(const unsigned char* p) {
    unsigned long long lo = rd32(p);
    unsigned long long hi = rd32(p + 4);
    return lo | (hi << 32);
}

static void wr16(unsigned char* p, unsigned short v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void wr32(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static int ext2_read_bytes(unsigned long long offset, unsigned int size, unsigned char* buffer) {
    if (!buffer || size == 0) {
        return 0;
    }
    readBytes(g_ext2.fs_start_addr + offset, size, (char*)buffer);
    return 1;
}

static int ext2_read_block(unsigned long long block, unsigned char* buffer) {
    unsigned long long lba;

    if (!buffer || g_ext2.block_sectors == 0) {
        return 0;
    }

    lba = g_ext2.fs_start_lba + (block * g_ext2.block_sectors);
    if (lba > 0xFFFFFFFFULL) {
        return 0;
    }

    dbg_u32("ext2_read_block lba=", (unsigned int)lba);

    readBytes(g_ext2.fs_start_addr + (block * (unsigned long long)g_ext2.block_size),
              g_ext2.block_size,
              (char*)buffer);
    return 1;
}

static int ext2_write_bytes(unsigned long long offset, unsigned int size, const unsigned char* buffer) {
    if (!buffer || size == 0) {
        return 0;
    }
    writeBytes(g_ext2.fs_start_addr + offset, size, (char*)buffer);
    return 1;
}

static int ext2_write_block(unsigned long long block, const unsigned char* buffer) {
    unsigned long long lba;

    if (!buffer || g_ext2.block_sectors == 0) {
        return 0;
    }

    lba = g_ext2.fs_start_lba + (block * g_ext2.block_sectors);
    if (lba > 0xFFFFFFFFULL) {
        return 0;
    }

    writeBytes(g_ext2.fs_start_addr + (block * (unsigned long long)g_ext2.block_size),
               g_ext2.block_size,
               (char*)buffer);
    return 1;
}

static unsigned long long ext2_group_desc_offset(unsigned int group) {
    return (g_ext2.gdt_start_block * (unsigned long long)g_ext2.block_size) +
           ((unsigned long long)group * g_ext2.group_desc_size);
}

static unsigned long long ext2_read_inode_table_block(unsigned int group) {
    unsigned char* desc;
    unsigned long long inode_table;

    desc = (unsigned char*)malloc(g_ext2.group_desc_size);
    if (!desc) {
        return 0;
    }

    if (!ext2_read_bytes(ext2_group_desc_offset(group), g_ext2.group_desc_size, desc)) {
        free(desc);
        return 0;
    }

    inode_table = rd32(desc + 8);
    if ((g_ext2.features_incompat & EXT2_FEATURE_INCOMPAT_64BIT) && g_ext2.group_desc_size >= 64) {
        unsigned long long hi = rd32(desc + 40);
        inode_table |= (hi << 32);
    }

    free(desc);
    return inode_table;
}

typedef struct {
    unsigned short mode;
    unsigned long long size;
    unsigned int block[15];
} Ext2Inode;

static int ext2_read_inode(unsigned int inode_num, Ext2Inode* out_inode) {
    unsigned int group;
    unsigned int index;
    unsigned long long inode_table_block;
    unsigned long long inode_offset;
    unsigned char* inode_buf;

    if (!out_inode || inode_num == 0) {
        return 0;
    }

    group = (inode_num - 1) / g_ext2.inodes_per_group;
    index = (inode_num - 1) % g_ext2.inodes_per_group;

    inode_table_block = ext2_read_inode_table_block(group);
    if (inode_table_block == 0) {
        return 0;
    }

    inode_offset = inode_table_block * (unsigned long long)g_ext2.block_size +
                   (unsigned long long)index * g_ext2.inode_size;

    inode_buf = (unsigned char*)malloc(g_ext2.inode_size);
    if (!inode_buf) {
        return 0;
    }

    if (!ext2_read_bytes(inode_offset, g_ext2.inode_size, inode_buf)) {
        free(inode_buf);
        return 0;
    }

    out_inode->mode = rd16(inode_buf + 0);
    out_inode->size = rd32(inode_buf + 4);

    if (g_ext2.inode_size >= 128) {
        unsigned long long size_high = rd32(inode_buf + 108);
        out_inode->size |= (size_high << 32);
    }

    for (int i = 0; i < 15; i++) {
        out_inode->block[i] = rd32(inode_buf + 40 + (i * 4));
    }

    free(inode_buf);
    return 1;
}

static int ext2_read_group_desc(unsigned int group, unsigned char* buf) {
    if (!buf || group >= g_ext2.groups_count) {
        return 0;
    }
    return ext2_read_bytes(ext2_group_desc_offset(group), g_ext2.group_desc_size, buf);
}

static int ext2_write_group_desc(unsigned int group, const unsigned char* buf) {
    if (!buf || group >= g_ext2.groups_count) {
        return 0;
    }
    return ext2_write_bytes(ext2_group_desc_offset(group), g_ext2.group_desc_size, buf);
}

static unsigned long long ext2_desc_block_field(const unsigned char* desc, unsigned int low_off, unsigned int high_off) {
    unsigned long long value = rd32(desc + low_off);
    if ((g_ext2.features_incompat & EXT2_FEATURE_INCOMPAT_64BIT) && g_ext2.group_desc_size >= 64) {
        unsigned long long hi = rd32(desc + high_off);
        value |= (hi << 32);
    }
    return value;
}

static int ext2_update_superblock_counts(int delta_blocks, int delta_inodes) {
    unsigned char sb[1024];
    if (!ext2_read_bytes(EXT2_SUPERBLOCK_OFFSET, 1024, sb)) {
        return 0;
    }

    unsigned int free_blocks = rd32(sb + 12);
    unsigned int free_inodes = rd32(sb + 16);

    free_blocks = (unsigned int)((int)free_blocks + delta_blocks);
    free_inodes = (unsigned int)((int)free_inodes + delta_inodes);

    wr32(sb + 12, free_blocks);
    wr32(sb + 16, free_inodes);

    return ext2_write_bytes(EXT2_SUPERBLOCK_OFFSET, 1024, sb);
}

static int ext2_update_group_counts(unsigned int group, int delta_blocks, int delta_inodes) {
    unsigned char desc[64];
    if (g_ext2.group_desc_size > sizeof(desc)) {
        return 0;
    }

    if (!ext2_read_group_desc(group, desc)) {
        return 0;
    }

    unsigned int free_blocks = rd16(desc + 12);
    unsigned int free_inodes = rd16(desc + 14);

    free_blocks = (unsigned int)((int)free_blocks + delta_blocks);
    free_inodes = (unsigned int)((int)free_inodes + delta_inodes);

    wr16(desc + 12, (unsigned short)free_blocks);
    wr16(desc + 14, (unsigned short)free_inodes);

    return ext2_write_group_desc(group, desc);
}

static int ext2_bitmap_set(unsigned long long bitmap_block, unsigned int index, int value) {
    if (!ext2_read_block(bitmap_block, g_ext2_index_buf)) {
        return 0;
    }

    unsigned int byte_index = index / 8;
    unsigned int bit_index = index % 8;
    unsigned char mask = (unsigned char)(1U << bit_index);

    if (value) {
        g_ext2_index_buf[byte_index] |= mask;
    } else {
        g_ext2_index_buf[byte_index] &= (unsigned char)(~mask);
    }

    return ext2_write_block(bitmap_block, g_ext2_index_buf);
}

static int ext2_bitmap_test(unsigned long long bitmap_block, unsigned int index) {
    if (!ext2_read_block(bitmap_block, g_ext2_index_buf)) {
        return 0;
    }

    unsigned int byte_index = index / 8;
    unsigned int bit_index = index % 8;
    unsigned char mask = (unsigned char)(1U << bit_index);

    return (g_ext2_index_buf[byte_index] & mask) != 0;
}

static int ext2_alloc_block(unsigned int* out_block) {
    unsigned char desc[64];
    unsigned long long bitmap_block;
    unsigned int group = 0;
    unsigned int blocks = g_ext2.blocks_per_group;

    if (!out_block || g_ext2.group_desc_size > sizeof(desc)) {
        return 0;
    }

    if (!ext2_read_group_desc(group, desc)) {
        return 0;
    }

    bitmap_block = ext2_desc_block_field(desc, 0, 32);
    if (bitmap_block == 0) {
        return 0;
    }

    if (!ext2_read_block(bitmap_block, g_ext2_index_buf)) {
        return 0;
    }

    for (unsigned int i = 0; i < blocks; i++) {
        unsigned int byte_index = i / 8;
        unsigned int bit_index = i % 8;
        unsigned char mask = (unsigned char)(1U << bit_index);
        if ((g_ext2_index_buf[byte_index] & mask) == 0) {
            g_ext2_index_buf[byte_index] |= mask;
            if (!ext2_write_block(bitmap_block, g_ext2_index_buf)) {
                return 0;
            }
            *out_block = g_ext2.first_data_block + i;
            ext2_update_superblock_counts(-1, 0);
            ext2_update_group_counts(group, -1, 0);
            return 1;
        }
    }

    return 0;
}

static int ext2_free_block(unsigned int block) {
    unsigned char desc[64];
    unsigned long long bitmap_block;
    unsigned int group;
    unsigned int index;

    if (block < g_ext2.first_data_block) {
        return 0;
    }

    group = (block - g_ext2.first_data_block) / g_ext2.blocks_per_group;
    index = (block - g_ext2.first_data_block) % g_ext2.blocks_per_group;

    if (group >= g_ext2.groups_count || g_ext2.group_desc_size > sizeof(desc)) {
        return 0;
    }

    if (!ext2_read_group_desc(group, desc)) {
        return 0;
    }

    bitmap_block = ext2_desc_block_field(desc, 0, 32);
    if (bitmap_block == 0) {
        return 0;
    }

    if (!ext2_bitmap_set(bitmap_block, index, 0)) {
        return 0;
    }

    ext2_update_superblock_counts(1, 0);
    ext2_update_group_counts(group, 1, 0);
    return 1;
}

static int ext2_alloc_inode(unsigned int* out_inode) {
    unsigned char desc[64];
    unsigned long long bitmap_block;
    unsigned int group = 0;
    unsigned int inodes = g_ext2.inodes_per_group;

    if (!out_inode || g_ext2.group_desc_size > sizeof(desc)) {
        return 0;
    }

    if (!ext2_read_group_desc(group, desc)) {
        return 0;
    }

    bitmap_block = ext2_desc_block_field(desc, 4, 36);
    if (bitmap_block == 0) {
        return 0;
    }

    if (!ext2_read_block(bitmap_block, g_ext2_index_buf)) {
        return 0;
    }

    for (unsigned int i = 0; i < inodes; i++) {
        unsigned int inode_num = group * g_ext2.inodes_per_group + i + 1;
        if (inode_num < g_ext2.first_inode) {
            continue;
        }
        unsigned int byte_index = i / 8;
        unsigned int bit_index = i % 8;
        unsigned char mask = (unsigned char)(1U << bit_index);
        if ((g_ext2_index_buf[byte_index] & mask) == 0) {
            g_ext2_index_buf[byte_index] |= mask;
            if (!ext2_write_block(bitmap_block, g_ext2_index_buf)) {
                return 0;
            }
            *out_inode = inode_num;
            ext2_update_superblock_counts(0, -1);
            ext2_update_group_counts(group, 0, -1);
            return 1;
        }
    }

    return 0;
}

static int ext2_free_inode(unsigned int inode_num) {
    unsigned char desc[64];
    unsigned long long bitmap_block;
    unsigned int group;
    unsigned int index;

    if (inode_num == 0) {
        return 0;
    }

    group = (inode_num - 1) / g_ext2.inodes_per_group;
    index = (inode_num - 1) % g_ext2.inodes_per_group;

    if (group >= g_ext2.groups_count || g_ext2.group_desc_size > sizeof(desc)) {
        return 0;
    }

    if (!ext2_read_group_desc(group, desc)) {
        return 0;
    }

    bitmap_block = ext2_desc_block_field(desc, 4, 36);
    if (bitmap_block == 0) {
        return 0;
    }

    if (!ext2_bitmap_set(bitmap_block, index, 0)) {
        return 0;
    }

    ext2_update_superblock_counts(0, 1);
    ext2_update_group_counts(group, 0, 1);
    return 1;
}

static int ext2_write_inode_raw(unsigned int inode_num, const unsigned char* buf) {
    unsigned int group;
    unsigned int index;
    unsigned long long inode_table_block;
    unsigned long long inode_offset;

    if (!buf || inode_num == 0) {
        return 0;
    }

    group = (inode_num - 1) / g_ext2.inodes_per_group;
    index = (inode_num - 1) % g_ext2.inodes_per_group;

    inode_table_block = ext2_read_inode_table_block(group);
    if (inode_table_block == 0) {
        return 0;
    }

    inode_offset = inode_table_block * (unsigned long long)g_ext2.block_size +
                   (unsigned long long)index * g_ext2.inode_size;

    return ext2_write_bytes(inode_offset, g_ext2.inode_size, buf);
}

static int ext2_write_inode_basic(unsigned int inode_num, unsigned short mode, unsigned long long size,
                                  unsigned int links, const unsigned int* blocks) {
    unsigned char inode_buf[256];
    unsigned int block_count = (unsigned int)((size + g_ext2.block_size - 1) / g_ext2.block_size);
    unsigned int i_blocks = block_count * (g_ext2.block_size / 512U);

    if (g_ext2.inode_size > sizeof(inode_buf)) {
        return 0;
    }

    for (unsigned int i = 0; i < g_ext2.inode_size; i++) {
        inode_buf[i] = 0;
    }

    wr16(inode_buf + 0, mode);
    wr32(inode_buf + 4, (unsigned int)(size & 0xFFFFFFFFULL));
    wr16(inode_buf + 26, (unsigned short)links);
    wr32(inode_buf + 28, i_blocks);
    if (g_ext2.inode_size >= 128) {
        wr32(inode_buf + 108, (unsigned int)(size >> 32));
    }

    for (int i = 0; i < 15; i++) {
        wr32(inode_buf + 40 + (i * 4), blocks ? blocks[i] : 0);
    }

    return ext2_write_inode_raw(inode_num, inode_buf);
}

static int ext2_update_inode_blocks(unsigned int inode_num, unsigned long long size, const unsigned int* blocks) {
    unsigned char inode_buf[256];
    unsigned int block_count = (unsigned int)((size + g_ext2.block_size - 1) / g_ext2.block_size);
    unsigned int i_blocks = block_count * (g_ext2.block_size / 512U);

    if (g_ext2.inode_size > sizeof(inode_buf)) {
        return 0;
    }

    if (!ext2_read_bytes(ext2_read_inode_table_block((inode_num - 1) / g_ext2.inodes_per_group) *
                         (unsigned long long)g_ext2.block_size +
                         (unsigned long long)((inode_num - 1) % g_ext2.inodes_per_group) * g_ext2.inode_size,
                         g_ext2.inode_size, inode_buf)) {
        return 0;
    }

    wr32(inode_buf + 4, (unsigned int)(size & 0xFFFFFFFFULL));
    wr32(inode_buf + 28, i_blocks);
    if (g_ext2.inode_size >= 128) {
        wr32(inode_buf + 108, (unsigned int)(size >> 32));
    }

    for (int i = 0; i < 15; i++) {
        wr32(inode_buf + 40 + (i * 4), blocks ? blocks[i] : 0);
    }

    return ext2_write_inode_raw(inode_num, inode_buf);
}

static int ext2_write_u32_to_block(unsigned int block, unsigned int index, unsigned int value) {
    unsigned int entries = g_ext2.block_size / 4;
    if (index >= entries) {
        return 0;
    }

    if (!ext2_read_block(block, g_ext2_index_buf)) {
        return 0;
    }

    wr32(g_ext2_index_buf + (index * 4), value);
    return ext2_write_block(block, g_ext2_index_buf);
}

static int ext2_read_u32_from_block(unsigned int block, unsigned int index, unsigned int* out_val);

static int ext2_zero_block(unsigned int block) {
    for (unsigned int i = 0; i < g_ext2.block_size; i++) {
        g_ext2_data_buf[i] = 0;
    }
    return ext2_write_block(block, g_ext2_data_buf);
}

static int ext2_set_block_ptr(unsigned int* blocks, unsigned int file_block, unsigned int data_block) {
    unsigned int entries = g_ext2.block_size / 4;

    if (file_block < 12) {
        blocks[file_block] = data_block;
        return 1;
    }

    file_block -= 12;

    if (file_block < entries) {
        if (blocks[12] == 0) {
            if (!ext2_alloc_block(&blocks[12])) {
                return 0;
            }
            if (!ext2_zero_block(blocks[12])) {
                return 0;
            }
        }
        return ext2_write_u32_to_block(blocks[12], file_block, data_block);
    }

    file_block -= entries;

    if (file_block < entries * entries) {
        unsigned int first_index = file_block / entries;
        unsigned int second_index = file_block % entries;
        unsigned int first_block;

        if (blocks[13] == 0) {
            if (!ext2_alloc_block(&blocks[13])) {
                return 0;
            }
            if (!ext2_zero_block(blocks[13])) {
                return 0;
            }
        }

        if (!ext2_read_u32_from_block(blocks[13], first_index, &first_block)) {
            return 0;
        }

        if (first_block == 0) {
            if (!ext2_alloc_block(&first_block)) {
                return 0;
            }
            if (!ext2_zero_block(first_block)) {
                return 0;
            }
            if (!ext2_write_u32_to_block(blocks[13], first_index, first_block)) {
                return 0;
            }
        }

        return ext2_write_u32_to_block(first_block, second_index, data_block);
    }

    file_block -= entries * entries;
    {
        unsigned int first_index = file_block / (entries * entries);
        unsigned int rem = file_block % (entries * entries);
        unsigned int second_index = rem / entries;
        unsigned int third_index = rem % entries;
        unsigned int first_block;
        unsigned int second_block;

        if (blocks[14] == 0) {
            if (!ext2_alloc_block(&blocks[14])) {
                return 0;
            }
            if (!ext2_zero_block(blocks[14])) {
                return 0;
            }
        }

        if (!ext2_read_u32_from_block(blocks[14], first_index, &first_block)) {
            return 0;
        }

        if (first_block == 0) {
            if (!ext2_alloc_block(&first_block)) {
                return 0;
            }
            if (!ext2_zero_block(first_block)) {
                return 0;
            }
            if (!ext2_write_u32_to_block(blocks[14], first_index, first_block)) {
                return 0;
            }
        }

        if (!ext2_read_u32_from_block(first_block, second_index, &second_block)) {
            return 0;
        }

        if (second_block == 0) {
            if (!ext2_alloc_block(&second_block)) {
                return 0;
            }
            if (!ext2_zero_block(second_block)) {
                return 0;
            }
            if (!ext2_write_u32_to_block(first_block, second_index, second_block)) {
                return 0;
            }
        }

        return ext2_write_u32_to_block(second_block, third_index, data_block);
    }
}

static void ext2_free_indirect_block(unsigned int block, int level) {
    if (block == 0 || level <= 0) {
        return;
    }

    unsigned int entries = g_ext2.block_size / 4;
    for (unsigned int i = 0; i < entries; i++) {
        unsigned int ptr = 0;
        if (!ext2_read_u32_from_block(block, i, &ptr)) {
            return;
        }
        if (ptr == 0) {
            continue;
        }
        if (level == 1) {
            ext2_free_block(ptr);
        } else {
            ext2_free_indirect_block(ptr, level - 1);
        }
    }

    ext2_free_block(block);
}

static void ext2_free_inode_blocks(const Ext2Inode* inode) {
    if (!inode) {
        return;
    }

    for (int i = 0; i < 12; i++) {
        if (inode->block[i]) {
            ext2_free_block(inode->block[i]);
        }
    }

    if (inode->block[12]) {
        ext2_free_indirect_block(inode->block[12], 1);
    }
    if (inode->block[13]) {
        ext2_free_indirect_block(inode->block[13], 2);
    }
    if (inode->block[14]) {
        ext2_free_indirect_block(inode->block[14], 3);
    }
}

static int ext2_read_u32_from_block(unsigned int block, unsigned int index, unsigned int* out_val) {
    unsigned int entries;

    if (!out_val) {
        return 0;
    }

    entries = g_ext2.block_size / 4;
    if (index >= entries) {
        return 0;
    }

    if (!ext2_read_block(block, g_ext2_index_buf)) {
        return 0;
    }

    *out_val = rd32(g_ext2_index_buf + (index * 4));
    return 1;
}

static int ext2_map_block(const Ext2Inode* inode, unsigned int file_block, unsigned int* out_block) {
    unsigned int entries;

    if (!inode || !out_block) {
        return 0;
    }

    entries = g_ext2.block_size / 4;

    if (file_block < 12) {
        *out_block = inode->block[file_block];
        return *out_block != 0;
    }

    file_block -= 12;

    if (file_block < entries) {
        if (inode->block[12] == 0) {
            return 0;
        }
        return ext2_read_u32_from_block(inode->block[12], file_block, out_block);
    }

    file_block -= entries;

    if (file_block < entries * entries) {
        unsigned int first_index = file_block / entries;
        unsigned int second_index = file_block % entries;
        unsigned int first_block;

        if (inode->block[13] == 0) {
            return 0;
        }

        if (!ext2_read_u32_from_block(inode->block[13], first_index, &first_block)) {
            return 0;
        }
        if (first_block == 0) {
            return 0;
        }
        return ext2_read_u32_from_block(first_block, second_index, out_block);
    }

    file_block -= entries * entries;

    {
        unsigned int first_index = file_block / (entries * entries);
        unsigned int rem = file_block % (entries * entries);
        unsigned int second_index = rem / entries;
        unsigned int third_index = rem % entries;
        unsigned int first_block;
        unsigned int second_block;

        if (inode->block[14] == 0) {
            return 0;
        }

        if (!ext2_read_u32_from_block(inode->block[14], first_index, &first_block)) {
            return 0;
        }
        if (first_block == 0) {
            return 0;
        }

        if (!ext2_read_u32_from_block(first_block, second_index, &second_block)) {
            return 0;
        }
        if (second_block == 0) {
            return 0;
        }

        return ext2_read_u32_from_block(second_block, third_index, out_block);
    }
}

static int ext2_dir_find(unsigned int dir_inode, const char* name, unsigned int name_len, unsigned int* out_inode, unsigned char* out_type) {
    Ext2Inode inode;
    unsigned char* block_buf;
    unsigned long long remaining;
    unsigned int block_index;

    if (!ext2_read_inode(dir_inode, &inode)) {
        return 0;
    }

    if ((inode.mode & EXT2_S_IFDIR) == 0) {
        return 0;
    }

    block_buf = (unsigned char*)malloc(g_ext2.block_size);
    if (!block_buf) {
        return 0;
    }

    remaining = inode.size;
    block_index = 0;

    while (remaining > 0) {
        unsigned int data_block;
        unsigned int to_read;
        unsigned int offset;

        if (!ext2_map_block(&inode, block_index, &data_block) || data_block == 0) {
            break;
        }

        if (!ext2_read_block(data_block, block_buf)) {
            break;
        }

        to_read = (remaining > g_ext2.block_size) ? g_ext2.block_size : (unsigned int)remaining;
        offset = 0;

        while (offset + 8 <= to_read) {
            unsigned int ent_inode = rd32(block_buf + offset);
            unsigned int rec_len = rd16(block_buf + offset + 4);
            unsigned int ent_name_len = block_buf[offset + 6];
            unsigned char ent_type = block_buf[offset + 7];

            if (rec_len == 0) {
                offset = to_read;
                continue;
            }

            if (ent_inode != 0 && ent_name_len == name_len) {
                int match = 1;
                for (unsigned int i = 0; i < name_len; i++) {
                    if (block_buf[offset + 8 + i] != (unsigned char)name[i]) {
                        match = 0;
                        break;
                    }
                }

                if (match) {
                    if (out_inode) {
                        *out_inode = ent_inode;
                    }
                    if (out_type) {
                        *out_type = ent_type;
                    }
                    free(block_buf);
                    return 1;
                }
            }

            if (rec_len < 8) {
                break;
            }

            offset += rec_len;
        }

        if (remaining > g_ext2.block_size) {
            remaining -= g_ext2.block_size;
        } else {
            remaining = 0;
        }
        block_index++;
    }

    free(block_buf);
    return 0;
}

static int ext2_lookup_path(const char* path, unsigned int* out_inode, unsigned char* out_type) {
    unsigned int current_inode = 2;
    unsigned char current_type = EXT2_FT_DIR;
    unsigned int i = 0;

    if (!path || path[0] == '\0') {
        return 0;
    }

    while (path[i] == '/') {
        i++;
    }

    if (path[i] == '\0') {
        if (out_inode) {
            *out_inode = current_inode;
        }
        if (out_type) {
            *out_type = current_type;
        }
        return 1;
    }

    while (path[i]) {
        char name[256];
        unsigned int name_len = 0;

        while (path[i] && path[i] != '/') {
            if (name_len + 1 >= sizeof(name)) {
                return 0;
            }
            name[name_len++] = path[i++];
        }
        name[name_len] = '\0';

        if (!ext2_dir_find(current_inode, name, name_len, &current_inode, &current_type)) {
            return 0;
        }

        while (path[i] == '/') {
            i++;
        }

        if (path[i] && current_type != EXT2_FT_DIR) {
            return 0;
        }
    }

    if (out_inode) {
        *out_inode = current_inode;
    }
    if (out_type) {
        *out_type = current_type;
    }
    return 1;
}

static int ext2_split_path(const char* path, char* parent, unsigned int parent_size, char* name, unsigned int name_size) {
    unsigned int len;
    unsigned int i;

    if (!path || !parent || !name || parent_size < 2 || name_size < 2) {
        return 0;
    }

    len = str_len((char*)path);
    if (len == 0) {
        return 0;
    }

    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    i = len;
    while (i > 0 && path[i - 1] != '/') {
        i--;
    }

    if (i == 0) {
        if (parent_size < 2 || name_size <= len) {
            return 0;
        }
        parent[0] = '/';
        parent[1] = 0;
        memcpy(name, path, len);
        name[len] = 0;
        return 1;
    }

    if (i == 1) {
        parent[0] = '/';
        parent[1] = 0;
    } else {
        if (i >= parent_size) {
            return 0;
        }
        memcpy(parent, path, i - 1);
        parent[i - 1] = 0;
    }

    unsigned int name_len = len - i;
    if (name_len + 1 > name_size) {
        return 0;
    }
    memcpy(name, path + i, name_len);
    name[name_len] = 0;
    return 1;
}

static unsigned int ext2_dir_entry_size(unsigned int name_len) {
    unsigned int size = 8 + name_len;
    return (size + 3) & ~3U;
}

static int ext2_add_dir_entry(unsigned int dir_inode, unsigned int child_inode, const char* name, unsigned char file_type) {
    Ext2Inode dir;
    unsigned int name_len;
    unsigned int entry_size;
    unsigned char* block_buf;
    unsigned long long remaining;
    unsigned int block_index;

    if (!name) {
        return 0;
    }

    name_len = str_len((char*)name);
    entry_size = ext2_dir_entry_size(name_len);

    if (!ext2_read_inode(dir_inode, &dir)) {
        return 0;
    }
    if ((dir.mode & EXT2_S_IFDIR) == 0) {
        return 0;
    }

    block_buf = g_ext2_data_buf;
    remaining = dir.size;
    block_index = 0;

    while (remaining > 0) {
        unsigned int data_block;
        unsigned int to_read;
        unsigned int offset;

        if (!ext2_map_block(&dir, block_index, &data_block) || data_block == 0) {
            break;
        }

        if (!ext2_read_block(data_block, block_buf)) {
            return 0;
        }

        to_read = (remaining > g_ext2.block_size) ? g_ext2.block_size : (unsigned int)remaining;
        offset = 0;

        while (offset + 8 <= to_read) {
            unsigned int ent_inode = rd32(block_buf + offset);
            unsigned int rec_len = rd16(block_buf + offset + 4);
            unsigned int ent_name_len = block_buf[offset + 6];

            if (rec_len < 8 || (rec_len & 3) != 0 || (offset + rec_len) > to_read) {
                break;
            }

            if (ent_inode == 0) {
                if (rec_len >= entry_size) {
                    wr32(block_buf + offset, child_inode);
                    wr16(block_buf + offset + 4, (unsigned short)rec_len);
                    block_buf[offset + 6] = (unsigned char)name_len;
                    block_buf[offset + 7] = file_type;
                    for (unsigned int i = 0; i < name_len; i++) {
                        block_buf[offset + 8 + i] = (unsigned char)name[i];
                    }
                    return ext2_write_block(data_block, block_buf);
                }
            } else {
                unsigned int used = ext2_dir_entry_size(ent_name_len);
                if (rec_len >= used + entry_size) {
                    unsigned int new_rec = rec_len - used;
                    wr16(block_buf + offset + 4, (unsigned short)used);

                    unsigned int new_off = offset + used;
                    wr32(block_buf + new_off, child_inode);
                    wr16(block_buf + new_off + 4, (unsigned short)new_rec);
                    block_buf[new_off + 6] = (unsigned char)name_len;
                    block_buf[new_off + 7] = file_type;
                    for (unsigned int i = 0; i < name_len; i++) {
                        block_buf[new_off + 8 + i] = (unsigned char)name[i];
                    }
                    return ext2_write_block(data_block, block_buf);
                }
            }

            offset += rec_len;
        }

        if (remaining > g_ext2.block_size) {
            remaining -= g_ext2.block_size;
        } else {
            remaining = 0;
        }
        block_index++;
    }

    unsigned int new_block;
    if (!ext2_alloc_block(&new_block)) {
        return 0;
    }

    for (unsigned int i = 0; i < g_ext2.block_size; i++) {
        block_buf[i] = 0;
    }

    wr32(block_buf + 0, child_inode);
    wr16(block_buf + 4, (unsigned short)g_ext2.block_size);
    block_buf[6] = (unsigned char)name_len;
    block_buf[7] = file_type;
    for (unsigned int i = 0; i < name_len; i++) {
        block_buf[8 + i] = (unsigned char)name[i];
    }

    if (!ext2_write_block(new_block, block_buf)) {
        return 0;
    }

    unsigned int new_blocks[15];
    for (int i = 0; i < 15; i++) {
        new_blocks[i] = dir.block[i];
    }

    unsigned int file_block = (unsigned int)(dir.size / g_ext2.block_size);
    if (!ext2_set_block_ptr(new_blocks, file_block, new_block)) {
        return 0;
    }

    dir.size += g_ext2.block_size;
    return ext2_update_inode_blocks(dir_inode, dir.size, new_blocks);
}

static int ext2_remove_dir_entry(unsigned int dir_inode, const char* name) {
    Ext2Inode dir;
    unsigned char* block_buf;
    unsigned long long remaining;
    unsigned int block_index;
    unsigned int name_len;

    if (!name) {
        return 0;
    }

    if (!ext2_read_inode(dir_inode, &dir)) {
        return 0;
    }

    if ((dir.mode & EXT2_S_IFDIR) == 0) {
        return 0;
    }

    name_len = str_len((char*)name);
    block_buf = g_ext2_data_buf;
    remaining = dir.size;
    block_index = 0;

    while (remaining > 0) {
        unsigned int data_block;
        unsigned int to_read;
        unsigned int offset;

        if (!ext2_map_block(&dir, block_index, &data_block) || data_block == 0) {
            break;
        }

        if (!ext2_read_block(data_block, block_buf)) {
            return 0;
        }

        to_read = (remaining > g_ext2.block_size) ? g_ext2.block_size : (unsigned int)remaining;
        offset = 0;

        while (offset + 8 <= to_read) {
            unsigned int ent_inode = rd32(block_buf + offset);
            unsigned int rec_len = rd16(block_buf + offset + 4);
            unsigned int ent_name_len = block_buf[offset + 6];

            if (rec_len < 8 || (rec_len & 3) != 0 || (offset + rec_len) > to_read) {
                break;
            }

            if (ent_inode != 0 && ent_name_len == name_len) {
                int match = 1;
                for (unsigned int i = 0; i < name_len; i++) {
                    if ((unsigned char)name[i] != block_buf[offset + 8 + i]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    wr32(block_buf + offset, 0);
                    dbg_puts("ext2_remove_dir_entry: write block\r\n");
                    return ext2_write_block(data_block, block_buf);
                }
            }

            offset += rec_len;
        }

        if (remaining > g_ext2.block_size) {
            remaining -= g_ext2.block_size;
        } else {
            remaining = 0;
        }
        block_index++;
    }

    return 0;
}

static int ext2_dir_is_empty(unsigned int dir_inode) {
    Ext2Inode dir;
    unsigned char* block_buf;
    unsigned long long remaining;
    unsigned int block_index;

    if (!ext2_read_inode(dir_inode, &dir)) {
        return 0;
    }

    if ((dir.mode & EXT2_S_IFDIR) == 0) {
        return 0;
    }

    block_buf = g_ext2_data_buf;
    remaining = dir.size;
    block_index = 0;

    while (remaining > 0) {
        unsigned int data_block;
        unsigned int to_read;
        unsigned int offset;

        if (!ext2_map_block(&dir, block_index, &data_block) || data_block == 0) {
            break;
        }

        if (!ext2_read_block(data_block, block_buf)) {
            return 0;
        }

        to_read = (remaining > g_ext2.block_size) ? g_ext2.block_size : (unsigned int)remaining;
        offset = 0;

        while (offset + 8 <= to_read) {
            unsigned int ent_inode = rd32(block_buf + offset);
            unsigned int rec_len = rd16(block_buf + offset + 4);
            unsigned int ent_name_len = block_buf[offset + 6];

            if (rec_len < 8 || (rec_len & 3) != 0 || (offset + rec_len) > to_read) {
                break;
            }

            if (ent_inode != 0 && ent_name_len > 0) {
                if (!(ent_name_len == 1 && block_buf[offset + 8] == '.') &&
                    !(ent_name_len == 2 && block_buf[offset + 8] == '.' && block_buf[offset + 9] == '.')) {
                    return 0;
                }
            }

            offset += rec_len;
        }

        if (remaining > g_ext2.block_size) {
            remaining -= g_ext2.block_size;
        } else {
            remaining = 0;
        }
        block_index++;
    }

    return 1;
}

int ext2_mount(unsigned int start_lba) {
    unsigned char* sb;
    unsigned int magic;
    unsigned int log_block_size;
    unsigned int desc_size;
    unsigned int rev_level;

    dbg_puts("ext2_mount: start\r\n");

    sb = (unsigned char*)malloc(1024);
    if (!sb) {
        dbg_puts("ext2_mount: malloc sb failed\r\n");
        return 0;
    }

    g_ext2.fs_start_lba = start_lba;
    g_ext2.fs_start_addr = (unsigned long long)start_lba * 512ULL;

    dbg_puts("ext2_mount: read superblock\r\n");
    if (!ext2_read_bytes(EXT2_SUPERBLOCK_OFFSET, 1024, sb)) {
        dbg_puts("ext2_mount: read superblock failed\r\n");
        free(sb);
        return 0;
    }
    dbg_puts("ext2_mount: superblock ok\r\n");

    magic = rd16(sb + 56);
    if (magic != EXT2_SUPER_MAGIC) {
        dbg_puts("ext2_mount: bad magic\r\n");
        free(sb);
        return 0;
    }

    log_block_size = rd32(sb + 24);
    g_ext2.block_size = 1024U << log_block_size;
    g_ext2.block_sectors = g_ext2.block_size / 512U;
    g_ext2.inodes_count = rd32(sb + 0);
    g_ext2.blocks_count = rd32(sb + 4);
    g_ext2.blocks_per_group = rd32(sb + 32);
    g_ext2.inodes_per_group = rd32(sb + 40);
    g_ext2.first_data_block = rd32(sb + 20);
    g_ext2.features_incompat = rd32(sb + 96);

    g_ext2.inode_size = rd16(sb + 88);
    if (g_ext2.inode_size == 0) {
        g_ext2.inode_size = 128;
    }

    g_ext2.first_inode = rd32(sb + 84);
    if (g_ext2.first_inode == 0) {
        g_ext2.first_inode = 11;
    }

    rev_level = rd32(sb + 76);
    if (rev_level >= 1) {
        desc_size = rd16(sb + 254);
        g_ext2.group_desc_size = (desc_size < 32) ? 32 : desc_size;
    } else {
        g_ext2.group_desc_size = 32;
    }

    if (g_ext2.block_size < 1024 || (g_ext2.block_size % 512) != 0) {
        dbg_puts("ext2_mount: unsupported block size\r\n");
        free(sb);
        return 0;
    }

    if (g_ext2.block_size > EXT2_BLOCK_BUF_MAX) {
        dbg_puts("ext2_mount: block size too large for static buffers\r\n");
        free(sb);
        return 0;
    }

    g_ext2.gdt_start_block = (g_ext2.block_size == 1024) ? 2 : 1;
    g_ext2.groups_count = (g_ext2.blocks_count + g_ext2.blocks_per_group - 1) / g_ext2.blocks_per_group;
    g_ext2.mounted = 1;

    dbg_puts("ext2_mount: ok\r\n");
    dbg_u32("block_size=", g_ext2.block_size);
    dbg_u32("inode_size=", g_ext2.inode_size);
    dbg_u32("blocks_per_group=", g_ext2.blocks_per_group);
    dbg_u32("inodes_per_group=", g_ext2.inodes_per_group);

    free(sb);
    return 1;
}

int ext2_read_file(const char* path, unsigned char* buffer, unsigned int max_bytes, unsigned int* out_size) {
    unsigned int inode_num;
    Ext2Inode inode;
    unsigned long long file_size;
    unsigned long long remaining;
    unsigned int block_index;
    unsigned char* block_buf;

    if (!g_ext2.mounted || !path) {
        return ext2_set_error(EXT2_ERR_NOT_MOUNTED);
    }

    if (!ext2_lookup_path(path, &inode_num, 0)) {
        ext2_dbg_last_error("ext2_read_file: lookup");
        return 0;
    }

    if (!ext2_read_inode(inode_num, &inode)) {
        ext2_set_error(EXT2_ERR_IO);
        ext2_dbg_last_error("ext2_read_file: inode");
        return 0;
    }

    if ((inode.mode & EXT2_S_IFREG) == 0) {
        ext2_set_error(EXT2_ERR_NOT_FILE);
        ext2_dbg_last_error("ext2_read_file: notfile");
        return 0;
    }

    file_size = inode.size;
    if (out_size) {
        *out_size = (unsigned int)((file_size > 0xFFFFFFFFULL) ? 0xFFFFFFFFULL : file_size);
    }

    if (!buffer || max_bytes == 0) {
        return 1;
    }

    if (file_size > max_bytes) {
        file_size = max_bytes;
    }

    block_buf = g_ext2_data_buf;

    remaining = file_size;
    block_index = 0;

    while (remaining > 0) {
        unsigned int data_block;
        unsigned int to_copy;
        unsigned int offset;

        if (!ext2_map_block(&inode, block_index, &data_block) || data_block == 0) {
            break;
        }

        if (!ext2_read_block(data_block, block_buf)) {
            break;
        }

        to_copy = (remaining > g_ext2.block_size) ? g_ext2.block_size : (unsigned int)remaining;
        offset = block_index * g_ext2.block_size;
        memcpy(buffer + offset, block_buf, to_copy);

        remaining -= to_copy;
        block_index++;
    }

    return 1;
}

int ext2_read_dir(const char* path, Ext2DirEntry** entries_out, unsigned int* out_count) {
    unsigned int inode_num;
    Ext2Inode inode;
    unsigned char* block_buf;
    unsigned long long remaining;
    unsigned int block_index;
    unsigned int count = 0;
    unsigned int capacity = 0;
    Ext2DirEntry* entries = 0;

    if (!g_ext2.mounted || !path || !entries_out || !out_count) {
        dbg_puts("ext2_read_dir: bad args or not mounted\r\n");
        return 0;
    }

    *entries_out = 0;
    *out_count = 0;

    if (!ext2_lookup_path(path, &inode_num, 0)) {
        dbg_puts("ext2_read_dir: lookup failed\r\n");
        return 0;
    }

    dbg_puts("ext2_read_dir: inode\r\n");
    dbg_u32("inode=", inode_num);

    if (!ext2_read_inode(inode_num, &inode)) {
        dbg_puts("ext2_read_dir: read inode failed\r\n");
        return 0;
    }

    if ((inode.mode & EXT2_S_IFDIR) == 0) {
        dbg_puts("ext2_read_dir: not a dir\r\n");
        return 0;
    }

    dbg_u32("dir_size=", (unsigned int)((inode.size > 0xFFFFFFFFULL) ? 0xFFFFFFFFULL : inode.size));
    dbg_u32("dir_block0=", inode.block[0]);

    dbg_puts("ext2_read_dir: use static block buf\r\n");
    block_buf = g_ext2_data_buf;

    dbg_puts("ext2_read_dir: alloc entries\r\n");
    capacity = 16;
    entries = (Ext2DirEntry*)malloc(sizeof(Ext2DirEntry) * capacity);
    if (!entries) {
        return 0;
    }

    dbg_puts("ext2_read_dir: start scan\r\n");

    remaining = inode.size;
    block_index = 0;

    while (remaining > 0) {
        unsigned int data_block;
        unsigned int to_read;
        unsigned int offset;

        dbg_u32("dir_block_index=", block_index);

        if (!ext2_map_block(&inode, block_index, &data_block) || data_block == 0) {
            dbg_puts("ext2_read_dir: map block failed\r\n");
            break;
        }

        dbg_u32("dir_block=", data_block);

        if (!ext2_read_block(data_block, block_buf)) {
            dbg_puts("ext2_read_dir: read block failed\r\n");
            break;
        }

        to_read = (remaining > g_ext2.block_size) ? g_ext2.block_size : (unsigned int)remaining;
        offset = 0;

        while (offset + 8 <= to_read) {
            unsigned int ent_inode = rd32(block_buf + offset);
            unsigned int rec_len = rd16(block_buf + offset + 4);
            unsigned int ent_name_len = block_buf[offset + 6];
            unsigned char ent_type = block_buf[offset + 7];

            if (rec_len < 8 || (rec_len & 3) != 0 || (offset + rec_len) > to_read) {
                dbg_puts("ext2_read_dir: bad rec_len\r\n");
                break;
            }

            if (ent_name_len > rec_len - 8) {
                dbg_puts("ext2_read_dir: bad name_len\r\n");
                break;
            }

            if (ent_inode != 0 && ent_name_len > 0) {
                if (count == capacity) {
                    unsigned int new_capacity = capacity * 2;
                    Ext2DirEntry* grown = (Ext2DirEntry*)malloc(sizeof(Ext2DirEntry) * new_capacity);
                    if (!grown) {
                        free(entries);
                        return 0;
                    }
                    memcpy(grown, entries, sizeof(Ext2DirEntry) * capacity);
                    free(entries);
                    entries = grown;
                    capacity = new_capacity;
                }
                unsigned int copy_len = (ent_name_len >= sizeof(entries[count].name))
                    ? (sizeof(entries[count].name) - 1)
                    : ent_name_len;

                entries[count].inode = ent_inode;
                entries[count].type = ent_type;
                for (unsigned int i = 0; i < copy_len; i++) {
                    entries[count].name[i] = (char)block_buf[offset + 8 + i];
                }
                entries[count].name[copy_len] = '\0';
                count++;
            }

            if (rec_len < 8) {
                break;
            }

            offset += rec_len;
        }

        if (remaining > g_ext2.block_size) {
            remaining -= g_ext2.block_size;
        } else {
            remaining = 0;
        }
        block_index++;
    }

    if (count == 0) {
        free(entries);
        entries = 0;
    }

    *entries_out = entries;
    *out_count = count;

    return 1;
}

static int ext2_write_file_data(const unsigned char* data, unsigned int size, unsigned int* out_blocks) {
    unsigned int blocks_needed;
    unsigned int data_offset = 0;

    if (!out_blocks) {
        return 0;
    }

    if (!data && size > 0) {
        return ext2_set_error(EXT2_ERR_INVALID);
    }

    for (int i = 0; i < 15; i++) {
        out_blocks[i] = 0;
    }

    blocks_needed = (size + g_ext2.block_size - 1) / g_ext2.block_size;

    if (blocks_needed == 0) {
        return 1;
    }

    for (unsigned int i = 0; i < blocks_needed; i++) {
        unsigned int block;
        unsigned int to_copy;

        if (!ext2_alloc_block(&block)) {
            ext2_set_error(EXT2_ERR_NO_SPACE);
            goto fail;
        }

        for (unsigned int j = 0; j < g_ext2.block_size; j++) {
            g_ext2_data_buf[j] = 0;
        }

        to_copy = (size - data_offset > g_ext2.block_size) ? g_ext2.block_size : (size - data_offset);
        if (data && to_copy > 0) {
            memcpy(g_ext2_data_buf, data + data_offset, to_copy);
        }

        if (!ext2_write_block(block, g_ext2_data_buf)) {
            ext2_free_block(block);
            ext2_set_error(EXT2_ERR_IO);
            goto fail;
        }

        if (!ext2_set_block_ptr(out_blocks, i, block)) {
            ext2_free_block(block);
            ext2_set_error(EXT2_ERR_IO);
            goto fail;
        }

        data_offset += to_copy;
    }

    return 1;

fail:
    {
        Ext2Inode tmp;
        for (int i = 0; i < 15; i++) {
            tmp.block[i] = out_blocks[i];
        }
        ext2_free_inode_blocks(&tmp);
    }
    return 0;
}

int ext2_write_file_overwrite(const char* path, const unsigned char* data, unsigned int size) {
    unsigned int inode_num;
    Ext2Inode inode;
    Ext2Inode old_inode;
    unsigned int blocks[15];

    g_ext2_last_error = EXT2_ERR_NONE;

    if (!g_ext2.mounted || !path) {
        return ext2_set_error(EXT2_ERR_NOT_MOUNTED);
    }

    if (!ext2_lookup_path(path, &inode_num, 0)) {
        return ext2_set_error(EXT2_ERR_NOT_FOUND);
    }

    if (!ext2_read_inode(inode_num, &inode)) {
        return ext2_set_error(EXT2_ERR_IO);
    }

    if ((inode.mode & EXT2_S_IFREG) == 0) {
        return ext2_set_error(EXT2_ERR_NOT_FILE);
    }

    old_inode = inode;

    if (!ext2_write_file_data(data, size, blocks)) {
        return 0;
    }

    if (!ext2_update_inode_blocks(inode_num, size, blocks)) {
        Ext2Inode tmp;
        for (int i = 0; i < 15; i++) {
            tmp.block[i] = blocks[i];
        }
        ext2_free_inode_blocks(&tmp);
        return ext2_set_error(EXT2_ERR_IO);
    }

    ext2_free_inode_blocks(&old_inode);
    return 1;
}

int ext2_create_file(const char* path, const unsigned char* data, unsigned int size) {
    char parent[256];
    char name[256];
    unsigned int parent_inode;
    unsigned char parent_type;
    unsigned int existing_inode;
    unsigned int inode_num;
    unsigned int blocks[15];

    g_ext2_last_error = EXT2_ERR_NONE;

    if (!g_ext2.mounted || !path) {
        return ext2_set_error(EXT2_ERR_NOT_MOUNTED);
    }

    if (!ext2_split_path(path, parent, sizeof(parent), name, sizeof(name))) {
        return ext2_set_error(EXT2_ERR_INVALID);
    }
    serial_write_string("ext2_delete: split path ok\r\n");

    if (!ext2_lookup_path(parent, &parent_inode, &parent_type)) {
        return ext2_set_error(EXT2_ERR_NOT_FOUND);
    }
    serial_write_string("ext2_delete: parent lookup ok\r\n");

    if (parent_type != EXT2_FT_DIR) {
        return ext2_set_error(EXT2_ERR_NOT_DIR);
    }

    if (ext2_dir_find(parent_inode, name, str_len(name), &existing_inode, 0)) {
        return ext2_set_error(EXT2_ERR_EXISTS);
    }

    if (!ext2_alloc_inode(&inode_num)) {
        return ext2_set_error(EXT2_ERR_NO_SPACE);
    }

    if (!ext2_write_file_data(data, size, blocks)) {
        ext2_free_inode(inode_num);
        return 0;
    }

    if (!ext2_write_inode_basic(inode_num, EXT2_S_IFREG, size, 1, blocks)) {
        Ext2Inode tmp;
        for (int i = 0; i < 15; i++) {
            tmp.block[i] = blocks[i];
        }
        ext2_free_inode_blocks(&tmp);
        ext2_free_inode(inode_num);
        return ext2_set_error(EXT2_ERR_IO);
    }

    if (!ext2_add_dir_entry(parent_inode, inode_num, name, EXT2_FT_REG_FILE)) {
        Ext2Inode tmp;
        for (int i = 0; i < 15; i++) {
            tmp.block[i] = blocks[i];
        }
        ext2_free_inode_blocks(&tmp);
        ext2_free_inode(inode_num);
        return ext2_set_error(EXT2_ERR_IO);
    }

    return 1;
}

int ext2_mkdir(const char* path) {
    char parent[256];
    char name[256];
    unsigned int parent_inode;
    unsigned char parent_type;
    unsigned int existing_inode;
    unsigned int inode_num;
    unsigned int blocks[15];
    unsigned int block;

    g_ext2_last_error = EXT2_ERR_NONE;

    if (!g_ext2.mounted || !path) {
        return ext2_set_error(EXT2_ERR_NOT_MOUNTED);
    }

    if (!ext2_split_path(path, parent, sizeof(parent), name, sizeof(name))) {
        return ext2_set_error(EXT2_ERR_INVALID);
    }

    if (!ext2_lookup_path(parent, &parent_inode, &parent_type)) {
        return ext2_set_error(EXT2_ERR_NOT_FOUND);
    }

    if (parent_type != EXT2_FT_DIR) {
        return ext2_set_error(EXT2_ERR_NOT_DIR);
    }

    if (ext2_dir_find(parent_inode, name, str_len(name), &existing_inode, 0)) {
        return ext2_set_error(EXT2_ERR_EXISTS);
    }

    if (!ext2_alloc_inode(&inode_num)) {
        return ext2_set_error(EXT2_ERR_NO_SPACE);
    }

    if (!ext2_alloc_block(&block)) {
        ext2_free_inode(inode_num);
        return ext2_set_error(EXT2_ERR_NO_SPACE);
    }

    for (unsigned int i = 0; i < g_ext2.block_size; i++) {
        g_ext2_data_buf[i] = 0;
    }

    unsigned int dot_len = 1;
    unsigned int dot_rec = ext2_dir_entry_size(dot_len);
    wr32(g_ext2_data_buf + 0, inode_num);
    wr16(g_ext2_data_buf + 4, (unsigned short)dot_rec);
    g_ext2_data_buf[6] = (unsigned char)dot_len;
    g_ext2_data_buf[7] = EXT2_FT_DIR;
    g_ext2_data_buf[8] = '.';

    unsigned int dotdot_len = 2;
    unsigned int dotdot_off = dot_rec;
    wr32(g_ext2_data_buf + dotdot_off, parent_inode);
    wr16(g_ext2_data_buf + dotdot_off + 4, (unsigned short)(g_ext2.block_size - dot_rec));
    g_ext2_data_buf[dotdot_off + 6] = (unsigned char)dotdot_len;
    g_ext2_data_buf[dotdot_off + 7] = EXT2_FT_DIR;
    g_ext2_data_buf[dotdot_off + 8] = '.';
    g_ext2_data_buf[dotdot_off + 9] = '.';

    if (!ext2_write_block(block, g_ext2_data_buf)) {
        ext2_free_block(block);
        ext2_free_inode(inode_num);
        return ext2_set_error(EXT2_ERR_IO);
    }

    for (int i = 0; i < 15; i++) {
        blocks[i] = 0;
    }
    blocks[0] = block;

    if (!ext2_write_inode_basic(inode_num, EXT2_S_IFDIR, g_ext2.block_size, 2, blocks)) {
        ext2_free_block(block);
        ext2_free_inode(inode_num);
        return ext2_set_error(EXT2_ERR_IO);
    }

    if (!ext2_add_dir_entry(parent_inode, inode_num, name, EXT2_FT_DIR)) {
        ext2_free_block(block);
        ext2_free_inode(inode_num);
        return ext2_set_error(EXT2_ERR_IO);
    }

    return 1;
}

int ext2_delete(const char* path) {
    char parent[256];
    char name[256];
    unsigned int parent_inode;
    unsigned char parent_type;
    unsigned int inode_num;
    unsigned char inode_type;
    Ext2Inode inode;

    g_ext2_last_error = EXT2_ERR_NONE;

    if (!g_ext2.mounted || !path) {
        return ext2_set_error(EXT2_ERR_NOT_MOUNTED);
    }

    if (!ext2_split_path(path, parent, sizeof(parent), name, sizeof(name))) {
        return ext2_set_error(EXT2_ERR_INVALID);
    }

    if (!ext2_lookup_path(parent, &parent_inode, &parent_type)) {
        return ext2_set_error(EXT2_ERR_NOT_FOUND);
    }

    if (parent_type != EXT2_FT_DIR) {
        return ext2_set_error(EXT2_ERR_NOT_DIR);
    }

    if (!ext2_dir_find(parent_inode, name, str_len(name), &inode_num, &inode_type)) {
        return ext2_set_error(EXT2_ERR_NOT_FOUND);
    }
    serial_write_string("ext2_delete: dir entry found\r\n");

    if (!ext2_read_inode(inode_num, &inode)) {
        return ext2_set_error(EXT2_ERR_IO);
    }
    serial_write_string("ext2_delete: inode read ok\r\n");

    if ((inode.mode & EXT2_S_IFDIR) != 0) {
        if (!ext2_dir_is_empty(inode_num)) {
            return ext2_set_error(EXT2_ERR_INVALID);
        }
    }
    serial_write_string("ext2_delete: dir empty ok\r\n");

    if (!ext2_remove_dir_entry(parent_inode, name)) {
        return ext2_set_error(EXT2_ERR_IO);
    }
    serial_write_string("ext2_delete: dir entry removed\r\n");

    serial_write_string("ext2_delete: freeing blocks\r\n");
    ext2_free_inode_blocks(&inode);
    serial_write_string("ext2_delete: freeing inode\r\n");
    if (!ext2_free_inode(inode_num)) {
        return ext2_set_error(EXT2_ERR_IO);
    }
    serial_write_string("ext2_delete: done\r\n");

    return 1;
}

static int ext2_join_path(const char* parent, const char* name, char* out, unsigned int out_size) {
    unsigned int parent_len;
    unsigned int name_len;
    unsigned int need_slash;

    if (!parent || !name || !out || out_size < 2) {
        return 0;
    }

    parent_len = str_len((char*)parent);
    name_len = str_len((char*)name);
    if (parent_len == 0) {
        return 0;
    }

    need_slash = (parent[parent_len - 1] != '/') ? 1U : 0U;
    if (parent_len + need_slash + name_len + 1 > out_size) {
        return 0;
    }

    memcpy(out, parent, parent_len);
    if (need_slash) {
        out[parent_len] = '/';
        parent_len++;
    }
    memcpy(out + parent_len, name, name_len);
    out[parent_len + name_len] = 0;
    return 1;
}

int ext2_delete_recursive(const char* path) {
    Ext2DirEntry* entries = 0;
    unsigned int count = 0;
    unsigned int i;

    if (!g_ext2.mounted || !path) {
        return ext2_set_error(EXT2_ERR_NOT_MOUNTED);
    }

    if (!ext2_read_dir(path, &entries, &count)) {
        return ext2_set_error(ext2_last_error());
    }

    for (i = 0; i < count; i++) {
        if (entries[i].name[0] == '.' && entries[i].name[1] == 0) {
            continue;
        }
        if (entries[i].name[0] == '.' && entries[i].name[1] == '.' && entries[i].name[2] == 0) {
            continue;
        }

        if (entries[i].type == EXT2_FT_DIR) {
            char child[256];
            if (!ext2_join_path(path, entries[i].name, child, sizeof(child))) {
                free(entries);
                return ext2_set_error(EXT2_ERR_INVALID);
            }
            if (!ext2_delete_recursive(child)) {
                free(entries);
                return 0;
            }
        } else {
            char child[256];
            if (!ext2_join_path(path, entries[i].name, child, sizeof(child))) {
                free(entries);
                return ext2_set_error(EXT2_ERR_INVALID);
            }
            if (!ext2_delete(child)) {
                free(entries);
                return 0;
            }
        }
    }

    free(entries);
    return ext2_delete(path);
}
