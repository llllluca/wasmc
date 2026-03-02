#define POLY 0xEDB88320
#define BUF_SIZE 15000

static unsigned int crc_table[256];
static char buffer[BUF_SIZE];

/* Initialize CRC lookup table */
void init_crc_table() {
    for (unsigned int i = 0; i < 256; i++) {
        unsigned int crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ POLY;
            else
                crc >>= 1;
        }
        crc_table[i] = crc;
    }
}

/* Fast CRC32 using lookup table */
unsigned int crc32_fast(const char *data, unsigned int length) {
    unsigned int crc = 0xFFFFFFFF;

    for (unsigned int i = 0; i < length; i++) {
        unsigned char index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc_table[index];
    }

    return crc ^ 0xFFFFFFFF;
}

/* Initialize deterministic 1MB test data */
void init_buffer() {
    for (unsigned int i = 0; i < BUF_SIZE; i++) {
        buffer[i] = (char)(i & 0xFF);   // repeating pattern 0..255
    }
}

int main() {
    init_crc_table();
    init_buffer();
    return crc32_fast(buffer, BUF_SIZE);
}
