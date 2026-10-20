file(GLOB_RECURSE vitagl_sources CONFIGURE_DEPENDS
     "third_party/vitaGL/source/*.c" "third_party/vitaGL/source/*.cpp")
file(GLOB_RECURSE vitagl_headers CONFIGURE_DEPENDS "third_party/vitaGL/source/*.h")

# Texture caches follow the source contents, including builds without Git.
set(vitagl_hashes "")
foreach(source IN LISTS vitagl_sources vitagl_headers)
  file(SHA256 "${source}" hash)
  string(APPEND vitagl_hashes "${hash}")
endforeach()
string(SHA256 PORT_VITAGL_REVISION "${vitagl_hashes}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${vitagl_sources} ${vitagl_headers})

add_library(vitaGL STATIC ${vitagl_sources})
set_target_properties(vitaGL PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
target_include_directories(vitaGL PRIVATE third_party/vitaGL/source)
target_compile_definitions(vitaGL PRIVATE SKIP_SPLASHSCREEN SKIP_ERROR_HANDLING)
target_compile_options(vitaGL PRIVATE
  -g -O3 -ffast-math -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -mfp16-format=ieee
  -Wno-stringop-overflow -UNDEBUG
  "$<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-pointer-types>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fexceptions;-Wno-write-strings>")
