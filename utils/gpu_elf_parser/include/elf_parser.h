/**
 * @file elf_parser.h
 * @brief This file contains the declaration of C functions for parsing ELF files.
 */

#ifndef FTRACE_ELF_PARSER_H_
#define FTRACE_ELF_PARSER_H_

#include <cstdint>

#include "elf_parser_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef enum {
  FTRACE_SUCCESS = 0,             //!< success
  FTRACE_ERROR_BAD_ARGUMENT = 1,  //!< invalid data
  FTRACE_ERROR_INTERNAL = 200,    //!< internal error

  /// New error types:
  FTRACE_DEBUG_INFO_NOT_FOUND = 16,  //!< debug info not found
} ftrace_result;

typedef void* elf_parser_handle_t;

/**
 * @brief Create parser
 * @param data - pointer to ELF data
 * @param size - size of data
 * @param parser - pointer to pointer parser handle
 * @return FTRACE_SUCCESS if success, FTRACE_ERROR_BAD_ARGUMENT if invalid argument, FTRACE_ERROR_INTERNAL if
 * internal error
 */
ftrace_result ftraceElfParserCreate(/*IN*/ const uint8_t* data, /*IN*/ uint32_t size,
                              /*OUT*/ elf_parser_handle_t* parser);

/**
 * @brief Delete parser
 * @param parser - pointer to pointer parser handle to delete
 */
ftrace_result ftraceElfParserDestroy(/*IN*/ elf_parser_handle_t* parser);

/**
 * @brief Check if parser is valid
 * @param parser - parser handle to check
 * @param is_valid - pointer to bool, where result is stored
 * @return FTRACE_SUCCESS if success, FTRACE_ERROR_BAD_ARGUMENT if invalid argument
 */
ftrace_result ftraceElfParserIsValid(/*IN*/ elf_parser_handle_t parser, /*OUT*/ bool* is_valid);

/**
 * @brief Get number of found kernel names and provides pointers to kernel names in original data
 * @param parser - parser handle.
 * @param num_entries - number of entries in names array. If names is nullptr, only num_names is
 * set. Should be greater than 0 in case names is not nullptr.
 * @param names - array of char pointers, points to null terminated strings inside original data.
 * Size of array should be at least num_entries. If names is nullptr, only num_names is set. The
 * number of kernel names returned is minimum of the value of num_entries and the actual number of
 * kernel names.
 * @param num_names - number of found kernel names. If num_names is nullptr, this argument is
 * ignored.
 * @return FTRACE_SUCCESS if success, FTRACE_ERROR_BAD_ARGUMENT if invalid argument
 */
ftrace_result ftraceElfParserGetKernelNames(/*IN*/ elf_parser_handle_t parser,
                                      /*IN*/ uint32_t num_entries,
                                      /*OUT*/ const char** names, /*OUT*/ uint32_t* num_names);

/**
 * @brief Get source mapping data for kernel with index kernel_index
 * @param parser - parser handle.
 * @param kernel_index - index of kernel
 * @param num_entries - number of entries in mappings array. If mappings is nullptr, only
 * num_mappings is set. Should be greater than 0 in case mappings is not nullptr.
 * @param mappings - array of SourceMapping structures. Size of array should be at least
 * num_entries. If mappings is nullptr, only num_mappings is set.
 * @param num_mappings - number of found mappings. If num_mappings is nullptr, this argument is
 * ignored.
 * @return FTRACE_SUCCESS if success, FTRACE_ERROR_BAD_ARGUMENT if invalid argument,
 * FTRACE_DEBUG_INFO_NOT_FOUND if no mapping found for specified kernel
 */
ftrace_result ftraceElfParserGetSourceMapping(/*IN*/ elf_parser_handle_t parser,
                                        /*IN*/ uint32_t kernel_index,
                                        /*IN*/ uint32_t num_entries,
                                        /*OUT*/ SourceMapping* mappings,
                                        /*OUT*/ uint32_t* num_mappings);

/**
 * @brief Get pointer to binary data in original data for kernel with index kernel_index. All
 * arguments are required.
 * @param parser - parser handle.
 * @param kernel_index - index of kernel.
 * @param binary - pointer to pointer to binary in original data.
 * @param binary_size - size of binary.
 * @param kernel_address - base address of kernel. Ignored if nullptr.
 * @return FTRACE_SUCCESS if success, FTRACE_ERROR_BAD_ARGUMENT if invalid argument, or FTRACE_ERROR_INTERNAL
 */
ftrace_result ftraceElfParserGetBinaryPtr(/*IN*/ elf_parser_handle_t parser, /*IN*/ uint32_t kernel_index,
                                    /*OUT*/ const uint8_t** binary, /*OUT*/ uint32_t* binary_size,
                                    /*OUT*/ uint64_t* kernel_address);

/**
 * @brief Returns value of GFX core from ELF file. The GFX core is stored in ELF file and can be
 * used to decode binary data (disassembly).
 * @param parser - parser handle.
 * @param gfx_core - pointer to store gfx core value
 * @return FTRACE_SUCCESS if success, FTRACE_ERROR_BAD_ARGUMENT if invalid argument
 */
ftrace_result ftraceElfParserGetGfxCore(/*IN*/ elf_parser_handle_t parser, /*OUT*/ uint32_t* gfx_core);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // FTRACE_ELF_PARSER_H_
