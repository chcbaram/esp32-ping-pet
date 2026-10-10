

file(GLOB SRC_FILES 
  bsp/*.c
  hw/*.c
)

file(GLOB_RECURSE SRC_FILES_RECURSE
  ap/*.c
  common/*.c 
  hw/driver/*.c
)


idf_component_register(
    SRCS "main.c"
    
    SRCS ${SRC_FILES}
    SRCS ${SRC_FILES_RECURSE}
    
    INCLUDE_DIRS ""
    INCLUDE_DIRS "ap"
    INCLUDE_DIRS "ap/modules"
    INCLUDE_DIRS "ap/modules/ui"
    INCLUDE_DIRS "ap/modules/ui/launcher"
    INCLUDE_DIRS "bsp"
    INCLUDE_DIRS "common"
    INCLUDE_DIRS "common/core"
    INCLUDE_DIRS "common/hw/include"
    INCLUDE_DIRS "hw"
    INCLUDE_DIRS "hw/driver/usb"
    INCLUDE_DIRS "hw/driver"

    # 폰트 (OFL) — ui_theme.c 가 Tiny TTF 로 크기마다 만든다
    EMBED_FILES "ap/modules/ui/fonts/Jua-Regular.ttf"
    EMBED_FILES "ap/modules/ui/fonts/Silkscreen-Regular.ttf"

    # MODULE_DEF 는 constructor 로 자기 등록한다. 다른 곳에서 참조하지 않는 모듈 파일을
    # 링커가 버리지 않도록 아카이브 전체를 링크한다 (ap/modules/module.h 참고)
    WHOLE_ARCHIVE
    )

# cli.h 의 CLI_USE(x) 는 매크로 안에서 defined 를 펼친다 (GCC 에서는 의도대로 동작한다)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-expansion-to-defined)
