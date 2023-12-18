# SPDX-License-Identifier: GPL-2.0-or-later
# -----------------------------------------------------------------------------
# Define the main source
# -----------------------------------------------------------------------------

set(inkscape_SRC
  alignment-snapper.cpp
  attribute-rel-css.cpp
  attribute-rel-svg.cpp
  attribute-rel-util.cpp
  attribute-sort-util.cpp
  attributes.cpp
  auto-save.cpp
  axis-manip.cpp
  hsluv.cpp
  color.cpp
  composite-undo-stack-observer.cpp
  conditions.cpp
  conn-avoid-ref.cpp
  console-output-undo-observer.cpp
  context-fns.cpp
  desktop-events.cpp
  desktop-style.cpp
  desktop.cpp
  distribution-snapper.cpp
  document-subset.cpp
  document-undo.cpp
  document.cpp
  event-log.cpp
  extract-uri.cpp
  file.cpp
  file-update.cpp
  filter-chemistry.cpp
  filter-enums.cpp
  gc-anchored.cpp
  gc-finalized.cpp
  gradient-chemistry.cpp
  gradient-drag.cpp
  guide-snapper.cpp
  grid-snapper.cpp
  id-clash.cpp
  inkscape.cpp
  inkscape-version-info.cpp
  layer-manager.cpp
  line-geometry.cpp
  line-snapper.cpp
  media.cpp
  message-context.cpp
  message-stack.cpp
  mod360.cpp
  object-hierarchy.cpp
  object-snapper.cpp
  oklab.cpp
  page-manager.cpp
  path-chemistry.cpp
  path-prefix.cpp
  perspective-line.cpp
  preferences.cpp
  print.cpp
  profile-manager.cpp
  proj_pt.cpp
  pure-transform.cpp
  rdf.cpp
  rubberband.cpp
  selcue.cpp
  selection-chemistry.cpp
  selection-describer.cpp
  selection.cpp
  seltrans-handles.cpp
  seltrans.cpp
  snap-preferences.cpp
  snap.cpp
  snapped-curve.cpp
  snapped-line.cpp
  snapped-point.cpp
  snapper.cpp
  style-internal.cpp
  style.cpp
  text-chemistry.cpp
  text-editing.cpp
  transf_mat_3x4.cpp
  unicoderange.cpp
  vanishing-point.cpp
  version.cpp

  # -------
  # Headers
  alignment-snapper.h
  attribute-rel-css.h
  attribute-rel-svg.h
  attribute-rel-util.h
  attribute-sort-util.h
  attributes.h
  auto-save.h
  axis-manip.h
  bad-uri-exception.h
  color-rgba.h
  hsluv.h
  color.h
  colorspace.h
  composite-undo-stack-observer.h
  conditions.h
  conn-avoid-ref.h
  console-output-undo-observer.h
  context-fns.h
  desktop-events.h
  desktop-style.h
  desktop.h
  distribution-snapper.h
  document-subset.h
  document-undo.h
  document.h
  enums.h
  event-log.h
  event.h
  extract-uri.h
  file.h
  fill-or-stroke.h
  filter-chemistry.h
  filter-enums.h
  gc-anchored.h
  gc-finalized.h
  gradient-chemistry.h
  gradient-drag.h
  guide-snapper.h
  grid-snapper.h
  id-clash.h
  inkscape-version.h
  inkscape-version-info.h
  inkscape.h
  layer-manager.h
  line-geometry.h
  line-snapper.h
  media.h
  message-context.h
  message-stack.h
  message.h
  mod360.h
  number-opt-number.h
  object-hierarchy.h
  object-snapper.h
  oklab.h
  page-manager.h
  path-chemistry.h
  path-prefix.h
  pattern-manager.cpp
  pattern-manager.h
  pattern-manipulation.cpp
  pattern-manipulation.h
  perspective-line.h
  preferences-skeleton.h
  preferences.h
  print.h
  profile-manager.h
  proj_pt.h
  pure-transform.h
  rdf.h
  rubberband.h
  selcue.h
  selection-chemistry.h
  selection-describer.h
  selection.h
  seltrans-handles.h
  seltrans.h
  snap-candidate.h
  snap-enums.h
  snap-preferences.h
  snap.h
  snapped-curve.h
  snapped-line.h
  snapped-point.h
  snapper.h
  streq.h
  strneq.h
  style-enums.h
  style-internal.h
  style.h
  syseq.h
  text-chemistry.h
  text-editing.h
  text-tag-attributes.h
  transf_mat_3x4.h
  undo-stack-observer.h
  unicoderange.h
  vanishing-point.h
  version.h

  # TEMP  Need to detangle inkscape-view from ui/interface.cpp
  inkscape-window.h
  inkscape-window.cpp

  inkscape-application.h
  inkscape-application.cpp
  actions/actions-effect-data.h
  actions/actions-effect-data.cpp
  actions/actions-extra-data.h
  actions/actions-extra-data.cpp
  actions/actions-hint-data.h
  actions/actions-hint-data.cpp

  actions/actions-base.h
  actions/actions-base.cpp
  actions/actions-canvas-mode.h
  actions/actions-canvas-mode.cpp
  actions/actions-canvas-snapping.h
  actions/actions-canvas-snapping.cpp
  actions/actions-canvas-transform.h
  actions/actions-canvas-transform.cpp
  actions/actions-dialogs.cpp
  actions/actions-dialogs.h
  actions/actions-edit-document.h
  actions/actions-edit-document.cpp
  actions/actions-edit-window.h
  actions/actions-edit-window.cpp
  actions/actions-edit.h
  actions/actions-edit.cpp
  actions/actions-effect.h
  actions/actions-effect.cpp
  actions/actions-element-a.h
  actions/actions-element-a.cpp
  actions/actions-element-image.h
  actions/actions-element-image.cpp
  actions/actions-file-window.h
  actions/actions-file-window.cpp
  actions/actions-file.h
  actions/actions-file.cpp
  actions/actions-helper.h
  actions/actions-helper.cpp
  actions/actions-helper-gui.h
  actions/actions-helper-gui.cpp
  actions/actions-help-url.h
  actions/actions-help-url.cpp
  actions/actions-hide-lock.h
  actions/actions-hide-lock.cpp
  actions/actions-layer.h
  actions/actions-layer.cpp
  actions/actions-node-align.h
  actions/actions-node-align.cpp
  actions/actions-object.h
  actions/actions-object.cpp
  actions/actions-object-align.h
  actions/actions-object-align.cpp
  actions/actions-output.h
  actions/actions-output.cpp
  actions/actions-paths.h
  actions/actions-paths.cpp
  actions/actions-selection-object.h
  actions/actions-selection-object.cpp
  actions/actions-selection-window.h
  actions/actions-selection-window.cpp
  actions/actions-selection.h
  actions/actions-selection.cpp
  actions/actions-text.h
  actions/actions-text.cpp
  actions/actions-tools.h
  actions/actions-tools.cpp
  actions/actions-tutorial.h
  actions/actions-tutorial.cpp
  actions/actions-transform.h
  actions/actions-transform.cpp
  actions/actions-undo-document.h
  actions/actions-undo-document.cpp
  actions/actions-view-mode.h
  actions/actions-view-mode.cpp
  actions/actions-view-window.h
  actions/actions-view-window.cpp
  actions/actions-window.h
  actions/actions-window.cpp
  actions/actions-pages.h
  actions/actions-pages.cpp
  actions/actions-svg-processing.h
  actions/actions-svg-processing.cpp

  inkview-application.h
  inkview-application.cpp
  inkview-window.h
  inkview-window.cpp

  manipulation/copy-resource.h
  manipulation/copy-resource.cpp
)
  
# -----------------------------------------------------------------------------
# Generate version file
# -----------------------------------------------------------------------------

# configure with values known at configure time
configure_file(inkscape-version.cpp.in ${CMAKE_BINARY_DIR}/src/inkscape-version.cpp.in @ONLY)

# configure further at build time (always built as run_always.txt is never actually created)
add_custom_command(
    OUTPUT inkscape-version.cpp run_always.txt
    COMMAND ${CMAKE_COMMAND}
        -DINKSCAPE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DINKSCAPE_BINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/CMakeScripts/inkscape-version.cmake
    COMMENT "Generating inkscape-version.cpp")

list(APPEND inkscape_SRC
  ${CMAKE_BINARY_DIR}/src/inkscape-version.cpp
)


# -----------------------------------------------------------------------------
# Load in subdirectories
# -----------------------------------------------------------------------------

# All folders for internal inkscape
# these call add_inkscape_source
add_subdirectory(actions)
add_subdirectory(async)
add_subdirectory(color)
add_subdirectory(debug)
add_subdirectory(display)
add_subdirectory(extension)
add_subdirectory(helper)
add_subdirectory(include)
add_subdirectory(io)
add_subdirectory(live_effects)
add_subdirectory(object)
add_subdirectory(path)
add_subdirectory(svg)
add_subdirectory(trace)
add_subdirectory(ui)
add_subdirectory(util)
add_subdirectory(util-string)
add_subdirectory(widgets)
add_subdirectory(xml)

# Directories containing lists files that describe building internal libraries
add_subdirectory(3rdparty)
add_subdirectory(inkgc)
add_subdirectory(livarot)
add_subdirectory(libnrtype)
include_directories("3rdparty/adaptagrams")

get_property(inkscape_global_SRC GLOBAL PROPERTY inkscape_global_SRC)

set(inkscape_SRC
  ${inkscape_global_SRC}
  ${inkscape_SRC}
)

# -----------------------------------------------------------------------------
# Setup the executable
# -----------------------------------------------------------------------------
set(main_SRC
  inkscape-main.cpp
)
set(view_SRC
  inkview-main.cpp
)

if(WIN32)
  # Configure and add the platform specific resource files (enabling the app icon).
  string(TIMESTAMP COPYRIGHT_YEAR %Y)
  
  set(FILE_NAME inkscape)
  set(PROGRAM_NAME Inkscape)
  set(PROGRAM_DESCRIPTION "Inkscape vector graphics editor")
  configure_file(inkscape.rc ${CMAKE_BINARY_DIR}/src/inkscape.rc)
  configure_file(inkscape-manifest.xml ${CMAKE_BINARY_DIR}/src/inkscape-manifest.xml)  
  list(APPEND main_SRC ${CMAKE_BINARY_DIR}/src/inkscape.rc)
  
  set(FILE_NAME inkview)
  set(PROGRAM_NAME Inkview)
  set(PROGRAM_DESCRIPTION "Inkview vector graphics viewer")
  configure_file(inkscape.rc ${CMAKE_BINARY_DIR}/src/inkview.rc)
  configure_file(inkscape-manifest.xml ${CMAKE_BINARY_DIR}/src/inkview-manifest.xml)
  list(APPEND view_SRC ${CMAKE_BINARY_DIR}/src/inkview.rc)
endif()

# make executables for inkscape and inkview
add_executable(inkscape ${main_SRC})
add_executable(inkview ${view_SRC})
if(WIN32)
  # Make the same executables again, but this time as console application (GUI applications can't print to the console)
  add_executable(inkscape_com ${main_SRC})
  set_target_properties(inkscape_com
                        PROPERTIES
                          LINK_FLAGS "-mconsole"
                          OUTPUT_NAME "inkscape.com"
                          SUFFIX "")
  add_executable(inkview_com ${view_SRC})
  set_target_properties(inkview_com
                        PROPERTIES
                          LINK_FLAGS "-mconsole"
                          OUTPUT_NAME "inkview.com"
                          SUFFIX "")
endif()


set(INKSCAPE_TARGET_LIBS
        # order from automake
        #sp_LIB
        #inkscape_LIB
        #sp_LIB # annoying, we need both!
        croco_LIB
        avoid_LIB
        cola_LIB
        vpsc_LIB
        livarot_LIB
        uemf_LIB
        depixelize_LIB
        autotrace_LIB
        util_LIB
        gc_LIB
)

# Build everything except main and inkview.c in a shared library.
add_library(inkscape_base ${inkscape_SRC} ${sp_SRC})
set_target_properties(inkscape_base PROPERTIES SOVERSION "${INKSCAPE_VERSION_MAJOR}.${INKSCAPE_VERSION_MINOR}.${INKSCAPE_VERSION_PATCH}.0")

# added so that locally installed inkscape can find lib2geom if it's not installed systemwide
if(NOT APPLE)
    set_target_properties(inkscape_base PROPERTIES INSTALL_RPATH "$ORIGIN/..")
else()
    set_target_properties(inkscape_base PROPERTIES INSTALL_RPATH "@loader_path/..")
endif()

target_link_libraries(inkscape_base PUBLIC Boost::filesystem)

# Link the inkscape_base library against all external dependencies
target_link_libraries(inkscape_base
    PRIVATE
        ${INKSCAPE_TARGET_LIBS}
        2Geom::2geom
    PUBLIC
        ${INKSCAPE_LIBS}
  )
target_include_directories(inkscape_base INTERFACE ${2Geom_INCLUDE_DIRS})

# Link inkscape and inkview against inkscape_base
target_link_libraries(inkscape inkscape_base)
target_link_libraries(inkview inkscape_base)
if(WIN32)
    target_link_libraries(inkscape_com inkscape_base)
    target_link_libraries(inkview_com inkscape_base)
endif()



#Define the installation
install(TARGETS inkscape inkview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32)
    install(TARGETS inkscape_com)
    install(TARGETS inkview_com)
endif()
if(BUILD_SHARED_LIBS)
    install(TARGETS inkscape_base LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/inkscape")
endif()
