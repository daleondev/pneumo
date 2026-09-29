# Glaze 7.0.2 predates C++26 optional iterators. Without this exclusion an
# optional matches both the nullable and array serializers/deserializers.
# Keep the pinned dependency reproducible, including FETCHCONTENT_SOURCE_DIR.
set(pnm_glaze_concepts "${glaze_SOURCE_DIR}/include/glaze/concepts/container_concepts.hpp")
file(READ "${pnm_glaze_concepts}" pnm_glaze_content)
set(pnm_glaze_old "concept range = requires(T& t) {")
set(pnm_glaze_new "concept range = !optional_like<T> && requires(T& t) {")
string(FIND "${pnm_glaze_content}" "${pnm_glaze_new}" pnm_glaze_patched)
if(pnm_glaze_patched EQUAL -1)
    string(FIND "${pnm_glaze_content}" "${pnm_glaze_old}" pnm_glaze_original)
    if(pnm_glaze_original EQUAL -1)
        message(FATAL_ERROR "Review GlazeCompatibility.cmake for the updated Glaze range concept")
    endif()
    string(REPLACE "${pnm_glaze_old}" "${pnm_glaze_new}" pnm_glaze_content "${pnm_glaze_content}")
    file(WRITE "${pnm_glaze_concepts}" "${pnm_glaze_content}")
endif()
