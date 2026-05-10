#ifndef SLOPPY_FUZZ_SUPPORT_H
#define SLOPPY_FUZZ_SUPPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SLOPPY_FUZZ_MAX_SEED_BYTES 262144U

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

#ifdef SLOPPY_FUZZ_STANDALONE
static int sloppy_fuzz_run_seed_file(const char* path)
{
    static uint8_t buffer[SLOPPY_FUZZ_MAX_SEED_BYTES];
    FILE* file = NULL;
    size_t bytes_read = 0U;

    if (path == NULL) {
        return 1;
    }

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL) {
        fprintf(stderr, "failed to open fuzz seed: %s\n", path);
        return 2;
    }

    bytes_read = fread(buffer, 1U, sizeof(buffer), file);
    if (ferror(file) != 0) {
        fclose(file);
        fprintf(stderr, "failed to read fuzz seed: %s\n", path);
        return 3;
    }

    if (fgetc(file) != EOF) {
        fclose(file);
        fprintf(stderr, "fuzz seed is larger than %u bytes: %s\n",
                (unsigned)SLOPPY_FUZZ_MAX_SEED_BYTES, path);
        return 4;
    }

    if (fclose(file) != 0) {
        return 5;
    }

    return LLVMFuzzerTestOneInput(buffer, bytes_read);
}

int main(int argc, char** argv)
{
    int index = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <seed> [seed...]\n", argv[0]);
        return 2;
    }

    for (index = 1; index < argc; index += 1) {
        int result = sloppy_fuzz_run_seed_file(argv[index]);
        if (result != 0) {
            return result;
        }
    }

    return 0;
}
#endif

#endif
