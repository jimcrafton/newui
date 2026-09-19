add_executable(example1
    example1.cpp
 )

target_link_libraries(example1 PRIVATE newui)

add_executable(delegates1
    delegates1.cpp
)

target_link_libraries(delegates1 PRIVATE newui)

add_executable(fonts1
    fonts1.cpp)

target_link_libraries(fonts1 PRIVATE newui)

add_executable(properties1
    properties1.cpp
)

target_link_libraries(properties1 PRIVATE newui)

add_executable(idletasks1
    idletasks1.cpp
)

target_link_libraries(idletasks1 PRIVATE newui)

add_executable(animation1
    animation1.cpp
)

target_link_libraries(animation1 PRIVATE newui)

add_executable(layout1
    layout1.cpp
)

target_link_libraries(layout1 PRIVATE newui)

add_executable(themes1
    themes1.cpp
)

target_link_libraries(themes1 PRIVATE newui)

add_executable(reflection1
    reflection1.cpp
)

target_link_libraries(reflection1 PRIVATE newui)

add_executable(reflection2
    reflection2.cpp
)

target_link_libraries(reflection2 PRIVATE newui)

add_executable(controls1
    controls1.cpp
)

target_link_libraries(controls1 PRIVATE newui)

add_executable(mvc1
    mvc1.cpp
)

target_link_libraries(mvc1 PRIVATE newui)

add_executable(uiinputmanager1
    uiinputmanager1.cpp
)

target_link_libraries(uiinputmanager1 PRIVATE newui)

add_executable(shapes1
    shapes1.cpp
)

target_link_libraries(shapes1 PRIVATE newui)

add_executable(shapes2
    shapes2.cpp
)

target_link_libraries(shapes2 PRIVATE newui)

add_executable(directwrite1
    directwrite1.cpp
)

target_link_libraries(directwrite1 PRIVATE newui d2d1 dwrite)


add_executable(custom_text_rndr
    custom_text_rndr.cpp
)

target_link_libraries(custom_text_rndr PRIVATE newui d2d1 dwrite)

add_executable(overlay1
    overlay1.cpp
)

target_link_libraries(overlay1 PRIVATE newui)

add_executable(imagefill1
    imagefill1.cpp
)

target_link_libraries(imagefill1 PRIVATE newui)

# st-enterprise-nc1701-a.png/robot1.png/robot2.png are source-tree assets
# (not generated), used via newui::Bundle::loadImage() - Bundle resolves
# relative to the running .exe's own directory (bundle.h's own class
# comment), so they need to actually live next to the built exe, not just
# in this source directory.
add_custom_command(TARGET imagefill1 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:imagefill1>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/st-enterprise-nc1701-a.png"
        "$<TARGET_FILE_DIR:imagefill1>/Resources/st-enterprise-nc1701-a.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/robot1.png"
        "$<TARGET_FILE_DIR:imagefill1>/Resources/robot1.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/robot2.png"
        "$<TARGET_FILE_DIR:imagefill1>/Resources/robot2.png"
)

add_executable(svgimage1
    svgimage1.cpp
)

target_link_libraries(svgimage1 PRIVATE newui)

# peacockspider.svg is a source-tree asset (svgandme's own gallery/ sample,
# wiladams/svgandme), used via both newui::gfx::Image's ".svg" constructors
# and newui::Bundle::loadImage() - same Resources/-relative post-build copy
# as imagefill1's photos above.
add_custom_command(TARGET svgimage1 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:svgimage1>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/peacockspider.svg"
        "$<TARGET_FILE_DIR:svgimage1>/Resources/peacockspider.svg"
)



add_executable(command1
    command1.cpp
)

target_link_libraries(command1 PRIVATE newui)

add_executable(clipboard1
    clipboard1.cpp
)

target_link_libraries(clipboard1 PRIVATE newui)

add_executable(dragdrop1
    dragdrop1.cpp
)

target_link_libraries(dragdrop1 PRIVATE newui)

add_executable(popuptool1
    popuptool1.cpp
)

target_link_libraries(popuptool1 PRIVATE newui)

# Phase 1b feasibility spike for swapchain-plan.md (DXGI flip-model swap
# chain presenting Blend2D pixels). d3d11/dxgi/dxguid are linked via
# #pragma comment(lib) in the source, not here - throwaway spike.
add_executable(swapchain1
    swapchain1.cpp
)

target_link_libraries(swapchain1 PRIVATE newui)

# Phase 1c: the same swap chain on a WS_CHILD whose parent is on another
# thread (the VSIX-host shape). Plain Win32, no newui View classes.
add_executable(swapchain2
    swapchain2.cpp
)

target_link_libraries(swapchain2 PRIVATE newui)

# Which PresentSurface (GDI / DXGI) a real RootView ended up with - see
# presentsurface.h. Toggle with a "gdi"/"dxgi" argument or NEWUI_PRESENT.
add_executable(presentbackend1
    presentbackend1.cpp
)

target_link_libraries(presentbackend1 PRIVATE newui)

# A standalone RootView (any PresentSurface) on a worker-thread RunLoop, as a
# WS_CHILD of a main-thread window - the cpp_codetools VSIX-host shape.
add_executable(crossthread1
    crossthread1.cpp
)

target_link_libraries(crossthread1 PRIVATE newui)

add_executable(loaddialog1
    loaddialog1.cpp
)

target_link_libraries(loaddialog1 PRIVATE newui)

# confirmdialog.newui (examples/, hand-written - see
# .claude/skills/newui-view-json5/SKILL.md) is this example's whole UI
# source of truth, loaded via Bundle::loadFrame() at runtime - same
# Resources/-relative post-build copy as popuptool1/svgimage1/imagefill1's
# own assets above.
add_custom_command(TARGET loaddialog1 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:loaddialog1>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/confirmdialog.newui"
        "$<TARGET_FILE_DIR:loaddialog1>/Resources/confirmdialog.newui"
)

add_executable(loadcoloreditor1
    loadcoloreditor1.cpp
)

target_link_libraries(loadcoloreditor1 PRIVATE newui)

# coloreditordialog.newui (examples/, generated from an HTML/CSS mockup via
# the newui-view-json5 skill's HTML-mockup-conversion process - see
# .claude/skills/newui-view-json5/references/html-mockup-conversion.md) is
# this example's whole UI source of truth, loaded via Bundle::loadFrame() at
# runtime - same Resources/-relative post-build copy as loaddialog1's own
# asset above.
add_custom_command(TARGET loadcoloreditor1 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:loadcoloreditor1>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/coloreditordialog.newui"
        "$<TARGET_FILE_DIR:loadcoloreditor1>/Resources/coloreditordialog.newui"
)

# popup-card.svg (examples/, a single rounded <rect>) is this popup's own
# shape source for the "SVG Popup" button, via Underlay::loadImageFile()
# (underlay.h) - same Resources/-relative post-build copy as svgimage1/
# imagefill1's own assets.
add_custom_command(TARGET popuptool1 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:popuptool1>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/popup-card.svg"
        "$<TARGET_FILE_DIR:popuptool1>/Resources/popup-card.svg"
)
