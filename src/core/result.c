#include "result.h"

const char *engine_result_name(EngineResult result)
{
    switch (result) {
        case ENGINE_RESULT_SUCCESS:
            return "ENGINE_RESULT_SUCCESS";
        case ENGINE_RESULT_INVALID_ARGUMENT:
            return "ENGINE_RESULT_INVALID_ARGUMENT";
        case ENGINE_RESULT_INVALID_STATE:
            return "ENGINE_RESULT_INVALID_STATE";
        case ENGINE_RESULT_OUT_OF_MEMORY:
            return "ENGINE_RESULT_OUT_OF_MEMORY";
        case ENGINE_RESULT_NOT_SUPPORTED:
            return "ENGINE_RESULT_NOT_SUPPORTED";
        default:
            return "ENGINE_RESULT_UNKNOWN";
    }
}
