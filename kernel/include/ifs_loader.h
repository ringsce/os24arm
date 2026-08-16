/**
 * @file ifs_loader.h
 * @brief IFS (Installable File System) Loader
 * @defgroup ifs IFS Loader
 *
 * Loads filesystem drivers from CONFIG.SYS directives like:
 *   IFS=EXT4.IFS
 *   IFS=HPFS.IFS /CACHE:2048
 */

#ifndef IFS_LOADER_H
#define IFS_LOADER_H

#include "types.h"
#include "vfs.h"

/* ── IFS Driver Structure ────────────────────────────────────────────────── */

/**
 * @brief IFS driver entry point signature
 *
 * Each IFS driver must export an initialization function:
 *   int ifs_init(const char *params)
 *
 * @param[in] params  Parameters from CONFIG.SYS (e.g., "/CACHE:2048")
 * @return 0 on success, negative on error
 */
typedef int (*ifs_init_fn)(const char *params);

/**
 * @brief IFS driver descriptor
 */
typedef struct {
    const char    *name;        /**< Driver name (e.g., "EXT4") */
    ifs_init_fn    init;        /**< Initialization function */
    vfs_fs_t      *fs_ops;      /**< Filesystem operations */
    void          *module_base; /**< Base address if dynamically loaded */
    uint32_t       module_size; /**< Module size */
} ifs_driver_t;

/* ── Built-in IFS Drivers ────────────────────────────────────────────────── */

/**
 * @brief Maximum number of registered IFS drivers
 */
#define IFS_MAX_DRIVERS 8

/* ── IFS Loader API ──────────────────────────────────────────────────────── */

/**
 * @brief Initialize IFS loader
 *
 * Sets up the IFS driver registry and prepares for CONFIG.SYS parsing.
 *
 * @pre VFS must be initialized
 * @post IFS loader ready to register drivers
 */
void ifs_loader_init(void);

/**
 * @brief Register a built-in IFS driver
 *
 * Registers a statically-linked IFS driver (e.g., FAT32, ext4).
 *
 * @param[in] name    Driver name (e.g., "EXT4")
 * @param[in] init_fn Initialization function
 * @param[in] fs_ops  Filesystem operations
 *
 * @return 0 on success, negative on error
 *
 * @code
 * // Register ext4 driver
 * extern int ext4_init(const char *params);
 * extern vfs_fs_t ext4_fs_ops;
 * ifs_register_builtin("EXT4", ext4_init, &ext4_fs_ops);
 * @endcode
 */
int ifs_register_builtin(const char *name, ifs_init_fn init_fn, vfs_fs_t *fs_ops);

/**
 * @brief Load IFS driver from CONFIG.SYS line
 *
 * Parses a CONFIG.SYS IFS= directive and loads the driver.
 *
 * @param[in] line  CONFIG.SYS line (e.g., "IFS=EXT4.IFS /CACHE:2048")
 *
 * @return 0 on success, negative on error
 *
 * Format: IFS=<name>.IFS [parameters]
 *
 * Examples:
 *   IFS=EXT4.IFS
 *   IFS=FAT32.IFS /LAZY
 *   IFS=HPFS.IFS /CACHE:2048 /AUTOCHECK
 */
int ifs_load_from_line(const char *line);

/**
 * @brief Parse and load all IFS drivers from CONFIG.SYS
 *
 * Reads CONFIG.SYS and loads all IFS= directives.
 *
 * @param[in] config_path  Path to CONFIG.SYS (e.g., "C:/CONFIG.SYS")
 *
 * @return Number of drivers loaded, negative on error
 */
int ifs_load_from_config(const char *config_path);

/**
 * @brief Get registered IFS driver by name
 *
 * @param[in] name  Driver name (e.g., "EXT4")
 *
 * @return Pointer to driver descriptor, NULL if not found
 */
ifs_driver_t* ifs_get_driver(const char *name);

/**
 * @brief List all registered IFS drivers
 *
 * Prints all loaded IFS drivers to console.
 */
void ifs_list_drivers(void);

#endif /* IFS_LOADER_H */