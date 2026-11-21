#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/checksum.h"

int main(void) {
    char hex[65];
    assert(checksum_parse_hex(
        "ba7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD  greed\n",
        hex, sizeof hex));
    assert(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    assert(!checksum_parse_hex("ba78", hex, sizeof hex));
    assert(!checksum_parse_hex(
        "zz7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex,
        sizeof hex));

    char tmpl[] = "/tmp/greed-sha-XXXXXX";
    int fd = mkstemp(tmpl);
    assert(fd >= 0);
    FILE *f = fdopen(fd, "w");
    assert(f);
    fputs("abc", f);
    fclose(f);
    assert(checksum_hash_file(tmpl, hex, sizeof hex));
    assert(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    unlink(tmpl);
    puts("checksum ok");
    return 0;
}
