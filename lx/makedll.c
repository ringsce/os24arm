/* ============================================================================
 * tools/makedll.c  —  Simple DLL Builder
 *
 * Usage: makedll input.bin output.dll dllname [export1 export2 ...]
 * ========================================================================== */

#include "lxformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        printf("Usage: %s input.bin output.dll dllname [export1 export2 ...]\n", argv[0]);
        printf("\n");
        printf("Creates an OS/2 LX DLL from binary code.\n");
        printf("\n");
        printf("Arguments:\n");
        printf("  input.bin   - Raw ARM64 binary code\n");
        printf("  output.dll  - Output LX DLL\n");
        printf("  dllname     - DLL module name (uppercase, no extension)\n");
        printf("  exportN     - Exported function names (optional)\n");
        printf("\n");
        printf("Example:\n");
        printf("  %s mylib.bin MYLIB.DLL MYLIB MyFunc1 MyFunc2 MyFunc3\n", argv[0]);
        printf("\n");
        printf("Note: Exports are assigned to offsets 0, 16, 32, ... automatically\n");
        return 1;
    }
    
    const char *input = argv[1];
    const char *output = argv[2];
    const char *dllname = argv[3];
    
    /* Collect exports */
    const char *exports[256];
    int export_count = 0;
    
    for (int i = 4; i < argc && export_count < 256; i++) {
        exports[export_count++] = argv[i];
    }
    
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
    
    printf("Creating DLL...\n");
    printf("  Input:   %s (%ld bytes)\n", input, size);
    printf("  Output:  %s\n", output);
    printf("  Name:    %s\n", dllname);
    printf("  Exports: %d\n", export_count);
    
    for (int i = 0; i < export_count; i++) {
        printf("    [%d] %s @ offset %d\n", i + 1, exports[i], i * 16);
    }
    
    /* Create DLL */
    int result = lx_create_dll(output, dllname, code, size, 
                               exports, export_count);
    
    free(code);
    
    if (result == 0) {
        printf("✓ Successfully created %s\n", output);
    } else {
        printf("✗ Failed to create DLL\n");
    }
    
    return result;
}
