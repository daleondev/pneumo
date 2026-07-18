function(pcmn_apply_platform_definitions target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "pcmn_apply_platform_definitions target does not exist: ${target}")
    endif()

    set(
        pcmn_x86_architectures
        amd64
        i386
        i686
        x86
        x86_64
    )

    set(
        pcmn_arm_architectures
        aarch64
        arm
        arm64
        arm64e
        armv6l
        armv7
        armv7l
    )

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" pcmn_system_processor)

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(pcmn_platform_name "Windows")
        set(pcmn_platform_definitions PNM_PLATFORM_WINDOWS)
        set(pcmn_allowed_arch_families x86 arm)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(pcmn_platform_name "macOS")
        set(pcmn_platform_definitions PNM_PLATFORM_MACOS PNM_PLATFORM_POSIX)
        set(pcmn_allowed_arch_families x86 arm)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(pcmn_platform_name "Linux")
        set(pcmn_platform_definitions PNM_PLATFORM_LINUX PNM_PLATFORM_POSIX)
        set(pcmn_allowed_arch_families x86 arm)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Generic")
        set(pcmn_platform_name "Generic")
        set(pcmn_platform_definitions PNM_PLATFORM_GENERIC PNM_PLATFORM_ARM)
        set(pcmn_allowed_arch_families arm)
    else()
        message(FATAL_ERROR "Unsupported operating system: ${CMAKE_SYSTEM_NAME}")
    endif()

    if(pcmn_system_processor IN_LIST pcmn_x86_architectures)
        set(pcmn_arch_family x86)
        set(pcmn_arch_definitions PNM_ARCH_X86)
    elseif(pcmn_system_processor IN_LIST pcmn_arm_architectures)
        set(pcmn_arch_family arm)
        set(pcmn_arch_definitions PNM_ARCH_ARM)
    else()
        message(FATAL_ERROR "Unsupported ${pcmn_platform_name} processor: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(NOT pcmn_arch_family IN_LIST pcmn_allowed_arch_families)
        message(FATAL_ERROR "Unsupported ${pcmn_platform_name} processor: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    target_compile_definitions(
        ${target}
        INTERFACE
        ${pcmn_platform_definitions}
        ${pcmn_arch_definitions}
    )
endfunction()