/* ============================================================================
 * tools/makeexe.c  —  Simple EXE Builder
 *
 * Usage: makeexe input.bin output.exe [entry_offset]
 * ========================================================================== */

#include "lxformat.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s input.bin output.exe [entry_offset]\n", argv[0]);
        printf("\n");
        printf("Creates a simple OS/2 LX executable from binary code.\n");
        printf("\n");
        printf("Arguments:\n");
        printf("  input.bin      - Raw ARM64 binary code\n");
        printf("  output.exe     - Output LX executable\n");
        printf("  entry_offset   - Entry point offset (hex, default: 0)\n");
        printf("\n");
        printf("Example:\n");
        printf("  %s hello.bin HELLO.EXE 0x1000\n", argv[0]);
        return 1;
    }
    
    const char *input = argv[1];
    const char *output = argv[2];
    uint32_t entry_offset = (argc > 3) ? strtoul(argv[3], NULL, 0) : 0;
    
    /* Read input file */
    FILE *f = fopen(input, "rb");
    if (!f) {
        perror("Failed to open input file");
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *code = malloc(size);
    if (!code) {
        fprintf(stderr, "Out of memory\n");
        fclose(f);
        return 1;
    }
    
    fread(code, 1, size, f);
    fclose(f);
    
    printf("Creating executable...\n");
    printf("  Input:  %s (%ld bytes)\n", input, size);
    printf("  Output: %s\n", output);
    printf("  Entry:  0x%X\n", entry_offset);
    
    /* Create EXE */
    int result = lx_create_exe(output, code, size, entry_offset, 0x10000);
    
    free(code);
    
    if (result == 0) {
        printf("✓ Successfully created %s\n", output);
    } else {
        printf("✗ Failed to create executable\n");
    }
    
    return result;
}
