#ifndef ENGINE_RESULT_H
#define ENGINE_RESULT_H

#include <assert.h>
#include <stdint.h>

/* Result/error type for all fallible engine operations.
 * See docs/spec/core/result.md for the contract.
 */

typedef enum EngineResult {
    ENGINE_RESULT_SUCCESS = 0,
    ENGINE_RESULT_INVALID_ARGUMENT,
    ENGINE_RESULT_INVALID_STATE,
    ENGINE_RESULT_OUT_OF_MEMORY,
    ENGINE_RESULT_NOT_SUPPORTED
} EngineResult;

/* Compiler-dependent nodiscard attribute.
 * C23 standardizes [[nodiscard]]; GCC/Clang provide __attribute__.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
    #define ENGINE_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
    #define ENGINE_NODISCARD __attribute__((warn_unused_result))
#else
    #define ENGINE_NODISCARD
#endif

/* RESULT-INV-1: ENGINE_RESULT_SUCCESS == 0 */
static_assert(ENGINE_RESULT_SUCCESS == 0, "ENGINE_RESULT_SUCCESS must be 0");

/* RESULT-INV-2: sizeof(EngineResult) == sizeof(int32_t) */
static_assert(sizeof(EngineResult) == sizeof(int32_t), "EngineResult must be exactly 32 bits");

/* RESULT-INV-3: All failure codes are positive */
static_assert(ENGINE_RESULT_INVALID_ARGUMENT > 0, "failure codes must be positive");
static_assert(ENGINE_RESULT_INVALID_STATE > 0, "failure codes must be positive");
static_assert(ENGINE_RESULT_OUT_OF_MEMORY > 0, "failure codes must be positive");
static_assert(ENGINE_RESULT_NOT_SUPPORTED > 0, "failure codes must be positive");

/* Return the string name of a result code for diagnostics and logging.
 * Returns "ENGINE_RESULT_UNKNOWN" for out-of-range values.
 * The returned pointer has static storage duration and must not be freed.
 * Never returns NULL (CORE-RESULT-011).
 */
ENGINE_NODISCARD const char *engine_result_name(EngineResult result);

#endif /* ENGINE_RESULT_H */
