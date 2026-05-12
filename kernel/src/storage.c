#include "storage.h"
#include "utils.h"
#include "vga.h"
#include "mem.h"

#define ATA_TIMEOUT 200000U

#define ATA_REG_DATA       0x1F0
#define ATA_REG_SECCOUNT0  0x1F2
#define ATA_REG_LBA0       0x1F3
#define ATA_REG_LBA1       0x1F4
#define ATA_REG_LBA2       0x1F5
#define ATA_REG_HDDEVSEL   0x1F6
#define ATA_REG_COMMAND    0x1F7
#define ATA_REG_STATUS     0x1F7
#define ATA_REG_ALTSTATUS  0x3F6

static void ata_io_wait(void) {
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
}

static void serial_write_hex64(uint64_t value) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        unsigned int nibble = (unsigned int)((value >> ((15 - i) * 4)) & 0xF);
        buf[2 + i] = (nibble < 10) ? (char)('0' + nibble) : (char)('a' + (nibble - 10));
    }
    buf[18] = 0;
    serial_write_string(buf);
}

static void serial_write_hex8(uint8_t value) {
    char buf[5];
    buf[0] = '0';
    buf[1] = 'x';
    unsigned int high = (unsigned int)((value >> 4) & 0xF);
    unsigned int low = (unsigned int)(value & 0xF);
    buf[2] = (high < 10) ? (char)('0' + high) : (char)('a' + (high - 10));
    buf[3] = (low < 10) ? (char)('0' + low) : (char)('a' + (low - 10));
    buf[4] = 0;
    serial_write_string(buf);
}

static int ata_select_lba28_drive(unsigned int lba) {
    outb(ATA_REG_HDDEVSEL, (unsigned char)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_io_wait();
    return ata_wait_busy();
}

int ata_wait_busy() {
    unsigned char status = 0;
    for (unsigned int i = 0; i < ATA_TIMEOUT; i++) {
        status = inb(ATA_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            return 1;
        }
    }
    serial_write_string("ATA timeout: BSY\r\n");
    char* status_str = int_to_str(status);
    if (status_str) {
        serial_write_string("ATA status=");
        serial_write_string(status_str);
        serial_write_string("\r\n");
        free(status_str);
    }
    return 0;
}

int ata_wait_drq() {
    unsigned char status = 0;
    for (unsigned int i = 0; i < ATA_TIMEOUT; i++) {
        status = inb(ATA_REG_STATUS);

        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            serial_write_string("ATA error/DF during DRQ wait\r\n");
            char* status_str = int_to_str(status);
            if (status_str) {
                serial_write_string("ATA status=");
                serial_write_string(status_str);
                serial_write_string("\r\n");
                free(status_str);
            }
            return 0;
        }

        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ)) {
            return 1;
        }
    }
    serial_write_string("ATA timeout: DRQ\r\n");
    char* status_str = int_to_str(status);
    if (status_str) {
        serial_write_string("ATA status=");
        serial_write_string(status_str);
        serial_write_string("\r\n");
        free(status_str);
    }
    return 0;
}

int ata_identify(unsigned short *identify_words) {
    if (!identify_words) {
        return 0;
    }

    serial_write_string("ata_identify: start\r\n");
    serial_write_string("ata_identify: buf=\r\n");
    serial_write_string("ata_identify: buf hex=");
    serial_write_hex64((uint64_t)identify_words);
    serial_write_string("\r\n");

    if (!ata_wait_busy()) {
        serial_write_string("ata_identify: wait busy failed\r\n");
        return 0;
    }

    outb(ATA_REG_HDDEVSEL, 0xA0);
    ata_io_wait();
    if (!ata_wait_busy()) {
        serial_write_string("ata_identify: wait busy after select failed\r\n");
        return 0;
    }

    outb(ATA_REG_SECCOUNT0, 0);
    outb(ATA_REG_LBA0, 0);
    outb(ATA_REG_LBA1, 0);
    outb(ATA_REG_LBA2, 0);

    outb(ATA_REG_COMMAND, 0xEC);
    ata_io_wait();
    if (!ata_wait_busy()) {
        serial_write_string("ata_identify: wait busy after command failed\r\n");
        return 0;
    }

    if (inb(ATA_REG_STATUS) == 0) {
        serial_write_string("ata_identify: status zero\r\n");
        return 0;
    }

    uint8_t lba1 = inb(ATA_REG_LBA1);
    uint8_t lba2 = inb(ATA_REG_LBA2);
    if (lba1 != 0 || lba2 != 0) {
        serial_write_string("ata_identify: not ATA lba1=");
        serial_write_hex8(lba1);
        serial_write_string(" lba2=");
        serial_write_hex8(lba2);
        serial_write_string("\r\n");
        return 0;
    }

    if (!ata_wait_drq()) {
        serial_write_string("ata_identify: wait drq failed\r\n");
        return 0;
    }

    for (int i = 0; i < 256; i++) {
        identify_words[i] = inw(ATA_REG_DATA);
    }

    serial_write_string("ata_identify: ok\r\n");

    return 1;
}

int ata_read_sector(unsigned int lba, unsigned char *buffer){
    if (!buffer) {
        return 0;
    }

    if (!ata_wait_busy()) {
        return 0;
    }

    if (!ata_select_lba28_drive(lba)) {
        return 0;
    }

    outb(ATA_REG_SECCOUNT0, 1);
    outb(ATA_REG_LBA0, lba & 0xFF);
    outb(ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_REG_LBA2, (lba >> 16) & 0xFF);

    outb(ATA_REG_COMMAND, 0x20);
    ata_io_wait();

    if (!ata_wait_drq()) {
        return 0;
    }

    for (int i = 0; i < 256;i++){
        unsigned short word = inw(ATA_REG_DATA);
        unsigned char low = (unsigned char)(word & 0xFF);
        unsigned char high = (unsigned char)(word >> 8);
        buffer[i*2] = low;
        buffer[i*2+1] = high;
    }

    return 1;
}

int ata_write_sector(unsigned int lba, unsigned char *buffer){
    if (!buffer) {
        return 0;
    }

    if (!ata_wait_busy()) {
        return 0;
    }

    if (!ata_select_lba28_drive(lba)) {
        return 0;
    }

    outb(ATA_REG_SECCOUNT0, 1);
    outb(ATA_REG_LBA0, lba & 0xFF);
    outb(ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_REG_LBA2, (lba >> 16) & 0xFF);

    outb(ATA_REG_COMMAND,0x30);
    ata_io_wait();

    if (!ata_wait_drq()) {
        return 0;
    }

    for(int i = 0; i < 256; i++){
        unsigned short word = (unsigned short)buffer[i * 2] |
                              ((unsigned short)buffer[i * 2 + 1] << 8);
        outw(ATA_REG_DATA, word);
    }

    outb(ATA_REG_COMMAND, 0xE7);
    ata_io_wait();
    if (!ata_wait_busy()) {
        return 0;
    }

    return 1;
}

int ata_read_sectors(unsigned int lba, unsigned char *buffer, unsigned int count){
    if (!buffer || count == 0 || count > 255) {
        return 0;
    }

    if (!ata_wait_busy()) {
        serial_write_string("ata_read_sectors: wait busy failed\r\n");
        return 0;
    }

    if (!ata_select_lba28_drive(lba)) {
        serial_write_string("ata_read_sectors: select drive failed\r\n");
        return 0;
    }

    outb(ATA_REG_SECCOUNT0, (unsigned char)count);
    outb(ATA_REG_LBA0, lba & 0xFF);
    outb(ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_REG_LBA2, (lba >> 16) & 0xFF);

    outb(ATA_REG_COMMAND, 0x20);
    ata_io_wait();

    for (unsigned int sector = 0; sector < count; sector++) {
        if (!ata_wait_drq()) {
            serial_write_string("ata_read_sectors: wait drq failed lba=");
            char* lba_str = int_to_str((int)lba);
            if (lba_str) {
                serial_write_string(lba_str);
                free(lba_str);
            }
            serial_write_string(" sector=");
            char* sec_str = int_to_str((int)sector);
            if (sec_str) {
                serial_write_string(sec_str);
                free(sec_str);
            }
            serial_write_string("\r\n");
            return 0;
        }

        unsigned char *sector_buffer = buffer + (sector * 512);
        for (int i = 0; i < 256; i++) {
            unsigned short word = inw(ATA_REG_DATA);
            sector_buffer[i * 2] = (unsigned char)(word & 0xFF);
            sector_buffer[i * 2 + 1] = (unsigned char)(word >> 8);
        }
    }

    return 1;
}
int ata_write_sectors(unsigned int lba, unsigned char *buffer, unsigned int count){
    if (!buffer || count == 0 || count > 255) {
        return 0;
    }

    if (!ata_wait_busy()) {
        serial_write_string("ata_write_sectors: wait busy failed\r\n");
        return 0;
    }

    if (!ata_select_lba28_drive(lba)) {
        serial_write_string("ata_write_sectors: select drive failed\r\n");
        return 0;
    }

    outb(ATA_REG_SECCOUNT0, (unsigned char)count);
    outb(ATA_REG_LBA0, lba & 0xFF);
    outb(ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_REG_LBA2, (lba >> 16) & 0xFF);

    outb(ATA_REG_COMMAND,0x30);
    ata_io_wait();

    for (unsigned int sector = 0; sector < count; sector++) {
        if (!ata_wait_drq()) {
            serial_write_string("ata_write_sectors: wait drq failed\r\n");
            return 0;
        }

        unsigned char *sector_buffer = buffer + (sector * 512);
        for (int i = 0; i < 256; i++) {
            unsigned short word = (unsigned short)sector_buffer[i * 2] |
                                  ((unsigned short)sector_buffer[i * 2 + 1] << 8);
            outw(ATA_REG_DATA, word);
        }
    }

    outb(ATA_REG_COMMAND, 0xE7);
    ata_io_wait();
    if (!ata_wait_busy()) {
        serial_write_string("ata_write_sectors: wait busy failed\r\n");
        return 0;
    }

    return 1;
}

static unsigned short ata_identify_buf[256];

int ata_smoke_test(void) {
    serial_write_string("ata_smoke_test: start\r\n");
    if (!ata_identify(ata_identify_buf)) {
        serial_write_string("ATA identify failed or no device\r\n");
        return 0;
    }

    unsigned char sector[512];
    if (!ata_read_sector(0, sector)) {
        serial_write_string("ATA read LBA0 failed\r\n");
        return 0;
    }

    serial_write_string("ATA smoke test ok\r\n");
    return 1;
}

void readBytes(unsigned long long addr,unsigned int bytes, char* buffer){
    int offset = 0;
    unsigned int lba = addrToLBA(addr,&offset);
    unsigned int amountOfSectors = (offset + bytes + 511) / 512;
    char * buff = malloc(amountOfSectors*512);
    serial_write_string("readBytes: lba=");
    char* lba_str = int_to_str((int)lba);
    if (lba_str) {
        serial_write_string(lba_str);
        free(lba_str);
    }
    serial_write_string(" count=");
    char* cnt_str = int_to_str((int)amountOfSectors);
    if (cnt_str) {
        serial_write_string(cnt_str);
        free(cnt_str);
    }
    serial_write_string("\r\n");
    for (unsigned int i = 0; i < amountOfSectors; i++) {
        if (!ata_read_sector(lba + i, (unsigned char*)buff + (i * 512))) {
            serial_write_string("readBytes: ata_read_sector failed\r\n");
            break;
        }
    }
    memcpy(buffer,buff+offset,bytes);
    free(buff);
}

void writeBytes(unsigned long long addr, unsigned int bytes, char* buff){
    int offset = 0;
    unsigned int lba = addrToLBA(addr,&offset);
    unsigned int amountOfSectors = (offset + bytes + 511) / 512;
    char* readbuff = malloc(amountOfSectors*512);
    for (unsigned int i = 0; i < amountOfSectors; i++) {
        if (!ata_read_sector(lba + i, (unsigned char*)readbuff + (i * 512))) {
            serial_write_string("writeBytes: ata_read_sector failed\r\n");
            break;
        }
    }
    memcpy(readbuff+offset,buff,bytes);
    for (unsigned int i = 0; i < amountOfSectors; i++) {
        if (!ata_write_sector(lba + i, (unsigned char*)readbuff + (i * 512))) {
            serial_write_string("writeBytes: ata_write_sector failed\r\n");
            break;
        }
    }
    free(readbuff);
}

unsigned int addrToLBA(unsigned long long addr, int* offset){
    *offset =  addr % 512;
    return addr / 512;
}

