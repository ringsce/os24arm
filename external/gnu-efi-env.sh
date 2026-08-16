# Source this file to use project-local gnu-efi
export PKG_CONFIG_PATH="/Users/pedro/CLionProjects/os2/external/gnu-efi-install/lib/pkgconfig:$PKG_CONFIG_PATH"
export GNUEFI_PREFIX="/Users/pedro/CLionProjects/os2/external/gnu-efi-install"
export EFI_INC="/Users/pedro/CLionProjects/os2/external/gnu-efi-install/include/efi"
export EFI_LIB="/Users/pedro/CLionProjects/os2/external/gnu-efi-install/lib"
export PATH="/Users/pedro/CLionProjects/os2/external/gnu-efi-install/bin:$PATH"

echo "gnu-efi environment configured:"
echo "  Prefix: /Users/pedro/CLionProjects/os2/external/gnu-efi-install"
echo "  Include: $EFI_INC"
echo "  Library: $EFI_LIB"
