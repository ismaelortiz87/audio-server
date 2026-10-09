// rapidyaml amalgamated single-header: the ONE translation unit that emits the
// library implementation.
//
// Upstream (see ryml_all.hpp header comment) requires exactly one source file
// in the program to define RYML_SINGLE_HDR_DEFINE_NOW before including the
// amalgamated header; every other user may include the header normally and pay
// only the (guarded) declaration cost.
//
// We additionally define RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS so that ryml's
// default error handler throws std::runtime_error instead of calling abort().
// A parser in an audio app must never abort the process on malformed user
// input. Both symbols only affect this TU (they gate the implementation
// section of the header), so consumers see an unchanged API.
//
// Guarded so the build still works if the whole project is ever compiled with
// -fno-exceptions.

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS
#endif

#define RYML_SINGLE_HDR_DEFINE_NOW
#include <ryml_all.hpp>
