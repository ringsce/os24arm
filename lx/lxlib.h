/* ============================================================================
* lxlib.h - LX Library Utility Header
 * ============================================================================
 * Utility functions for working with LX format files
 */

#ifndef LXLIB_H
#define LXLIB_H

#include <stdint.h>
#include <stdio.h>

/* Function prototypes */

/**
 * Display information about an LX file
 */
int lxlib_info(const char *filename);

/**
 * Extract sections from an LX file
 */
int lxlib_extract(const char *filename, const char *output_dir);

/**
 * Verify LX file structure
 */
int lxlib_verify(const char *filename);

/**
 * List symbols/exports from an LX file
 */
int lxlib_symbols(const char *filename);

/**
 * Convert LX to another format
 */
int lxlib_convert(const char *input, const char *output, const char *format);

#endif /* LXLIB_H */