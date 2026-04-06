#include "utils.h"
#include "interupts.h"
#include "mem.h"

void str_cp(char* str1, char* str2){
    while (*str1){
        *str2 = *str1;
        str1++;
        str2++;
    }
    *str2 = '\0';
}

unsigned int str_len(char* str){
    unsigned int len = 0;
    while (*str++)
    {
        len++;
    }
    return len;
}

void cp_buff(unsigned char* src, unsigned char* dest, unsigned int size){
    for (unsigned int i = 0; i < size; i++){
        dest[i] = src[i];
    }
}

void trim(char* str){
    unsigned int len = str_len(str);
    while (len > 0) {
        char last = str[len - 1];
        if (last == '\n' || last == '\r' || last == ' ' || last == '\t') {
            str[len - 1] = 0;
            len--;
        } else {
            break;
        }
    }
}
char str_cmp(char* str1, char* str2){
    if (str_len(str1)!=str_len(str2)){
        return 0;
    }
    while (*str1)
    {
        if (*str1 != *str2){
            return 0;
        }
        
        str1++;
        str2++;
    }
    return 1;
}
unsigned int split(char** buff, unsigned int buffSize, char c, char* str) {
    unsigned int count = 0;
    char* start = str;

    while (*str) {
        if (*str == c) {
            if (count >= buffSize) break;

            unsigned int len = str - start;
            buff[count] = (char*)malloc(len + 1); // allocate space
            for (unsigned int i = 0; i < len; i++) {
                buff[count][i] = start[i];
            }
            buff[count][len] = '\0';
            count++;

            start = str + 1; // next token
        }
        str++;
    }

    // last token
    if (*start && count < buffSize) {
        unsigned int len = str - start;
        buff[count] = (char*)malloc(len + 1);
        for (unsigned int i = 0; i < len; i++) {
            buff[count][i] = start[i];
        }
        buff[count][len] = '\0';
        count++;
    }

    return count;
}
void* memcpy(void* dest, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    for (unsigned int i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dest;
}
double ceil(double x) {
    int i = (int)x;

    if (x > (double)i) {
        return (double)(i + 1);
    }

    return (double)i;
}
double floor(double x) {
    int i = (int)x;

    if (x < (double)i) {
        return (double)(i - 1);
    }

    return (double)i;
}