include("${VITASDK}/share/vita.cmake")

vita_create_self(eboot.bin plague_vita UNSAFE NOASLR)
vita_create_vpk(plague-vita.vpk PLAGUEINC eboot.bin
  VERSION 01.00
  NAME "Plague Inc. Vita"
  FILE "${PORT_ASSET_DIR}/package/icon0.png" sce_sys/icon0.png
  FILE "${PORT_ASSET_DIR}/package/pic0.png" sce_sys/pic0.png
  FILE "${PORT_ASSET_DIR}/package/bg0.png" sce_sys/livearea/contents/bg0.png
  FILE "${PORT_ASSET_DIR}/package/template.xml" sce_sys/livearea/contents/template.xml
  FILE "${PORT_ASSET_DIR}/trophy/PLGV00001_00/TROPHY.TRP" sce_sys/trophy/PLGV00001_00/TROPHY.TRP
  FILE "${PORT_ASSET_DIR}/trophy/PLGV00001_01/TROPHY.TRP" sce_sys/trophy/PLGV00001_01/TROPHY.TRP
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bionic/LICENSE" licenses/bionic/LICENSE
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jni/LICENSE" licenses/jni/LICENSE
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minizip/LICENSE" licenses/minizip/LICENSE
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/so_util/LICENSE" licenses/so_util/LICENSE
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vitaGL/COPYING" licenses/vitaGL/COPYING
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vitaGL/COPYING.LESSER" licenses/vitaGL/COPYING.LESSER
  FILE "${VITASDK}/arm-vita-eabi/share/licenses/SDL2/LICENSE.txt" licenses/SDL2/LICENSE.txt
  FILE "${VITASDK}/arm-vita-eabi/share/licenses/SDL2_mixer/LICENSE.txt" licenses/SDL2_mixer/LICENSE.txt
  FILE "${VITASDK}/arm-vita-eabi/share/doc/opusfile/COPYING" licenses/opusfile/COPYING)
