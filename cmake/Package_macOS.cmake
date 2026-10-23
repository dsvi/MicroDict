set(PLIST_FILE "${CMAKE_BINARY_DIR}/info_generated.plist")
configure_file("${CMAKE_SOURCE_DIR}/redist/mac_info_plist_template_cmake.plist" "${PLIST_FILE}" @ONLY)

set_target_properties(${MICRODICT} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${PLIST_FILE}"
)

set(Assembling_Dir "${CMAKE_BINARY_DIR}/redist")
set(App_Name "${MICRODICT}.app")
set(Redistributable_APP "${Assembling_Dir}/${App_Name}")

set(QtConfPath "${Redistributable_APP}/Contents/Resources/qt.conf")

set(MACOS_DEPLOY_ICU_SETUP "")
if (BREW_ICU_ADDITIONAL_DYLIBS)
    string(REPLACE ";" "\" \"" BREW_ICU_COPY_ARGUMENTS "${BREW_ICU_ADDITIONAL_DYLIBS}")
    set(BREW_ICU_COPY_ARGUMENTS "\"${BREW_ICU_COPY_ARGUMENTS}\"")
    set(MACOS_DEPLOY_ICU_SETUP
            "file(MAKE_DIRECTORY \"${Redistributable_APP}/Contents/Frameworks\")
             file(COPY ${BREW_ICU_COPY_ARGUMENTS}
                  DESTINATION \"${Redistributable_APP}/Contents/Frameworks\"
                  FOLLOW_SYMLINK_CHAIN)")
endif ()

qt_generate_deploy_script(
        TARGET ${MICRODICT}
        OUTPUT_SCRIPT deploy_script
        CONTENT "
        set(QT_DEPLOY_PREFIX \"${Redistributable_APP}\")
        set(QT_DEPLOY_TRANSLATIONS_DIR \"Contents/Resources/translations\")
        ${MACOS_DEPLOY_ICU_SETUP}
        qt_deploy_runtime_dependencies(
                    EXECUTABLE \"${Redistributable_APP}\"
                    GENERATE_QT_CONF
                    NO_APP_STORE_COMPLIANCE)
        file(REMOVE
             \"${Redistributable_APP}/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib\")
        qt_deploy_translations()
        qt_deploy_qt_conf(\"${QtConfPath}\"
                     PLUGINS_DIR PlugIns
                     TRANSLATIONS_DIR Resources/translations)
        "
)

install(TARGETS ${MICRODICT} BUNDLE DESTINATION "${Assembling_Dir}")

if (IS_READABLE "/opt/homebrew/share/opencc/")
    set(OPENCC_DATA_PATH "/opt/homebrew/share/opencc/" CACHE PATH "opencc's data path")
elseif (IS_READABLE "/usr/local/share/opencc/")
    set(OPENCC_DATA_PATH "/usr/local/share/opencc/" CACHE PATH "opencc's data path")
else ()
    message(FATAL_ERROR "Cannot find opencc's data folder!")
endif ()

file(REAL_PATH "${OPENCC_DATA_PATH}" OPENCC_DATA_PATH_FOR_REAL)

message(STATUS "OPENCC data is found -> ${OPENCC_DATA_PATH_FOR_REAL}")
install(DIRECTORY "${OPENCC_DATA_PATH_FOR_REAL}" DESTINATION "${Redistributable_APP}/Contents/MacOS")

install(SCRIPT ${deploy_script})

install(CODE "
    execute_process(
        COMMAND codesign --force --deep --sign - \"${Redistributable_APP}\"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(
        COMMAND codesign --verify --deep --strict \"${Redistributable_APP}\"
        COMMAND_ERROR_IS_FATAL ANY)
")

find_program(CREATE-DMG "create-dmg" HINTS "/opt/homebrew/bin" "/usr/local/bin")
if (CREATE-DMG)
    set(DMG_OUTPUT_NAME "MicroDict-${CMAKE_PROJECT_VERSION}-Qt${Qt6_VERSION}-macOS-${CMAKE_SYSTEM_PROCESSOR}.dmg")
    install(CODE "
    file(REMOVE \"${CMAKE_CURRENT_BINARY_DIR}/${DMG_OUTPUT_NAME}\")
    execute_process(COMMAND ${CREATE-DMG} \
        --skip-jenkins \
        --format \"ULMO\"
        --volname ${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR} \
        --volicon ${CMAKE_SOURCE_DIR}/icons/macicon.icns \
        --icon \"${App_Name}\" 100 100
        --app-drop-link 300 100 \
        \"${DMG_OUTPUT_NAME}\" \
        \"${Assembling_Dir}\"
        COMMAND_ERROR_IS_FATAL ANY)"
    )
else ()
    message(WARNING "create-dmg not found. Install it via `brew install create-dmg` before running cmake --install, otherwise no .dmg artifact will be produced (the .app bundle is still produced).")
endif ()
