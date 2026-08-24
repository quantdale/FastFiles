# Resolve a PowerShell executable that CTest can launch as a native child.
# Store-installed PowerShell can leave find_program() pointing at the package
# binary under Program Files\WindowsApps. On some Windows hosts CTest cannot
# CreateProcess that path, while the per-user execution alias works normally.
# Prefer the alias only when the cached/discovered path is the package binary;
# preserve an explicitly usable non-package installation.
function(ff_resolve_powershell out_variable)
    set(alias_path "")
    if(DEFINED ENV{LOCALAPPDATA})
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps/pwsh.exe" alias_path)
    endif()

    if(EXISTS "${alias_path}" AND
       (NOT DEFINED ${out_variable} OR
        NOT EXISTS "${${out_variable}}" OR
        "${${out_variable}}" MATCHES "[/\\\\]WindowsApps[/\\\\]"))
        set(${out_variable} "${alias_path}" CACHE FILEPATH
            "PowerShell executable used by native CTest children" FORCE)
    endif()

    if(NOT DEFINED ${out_variable} OR NOT EXISTS "${${out_variable}}")
        find_program(discovered_powershell NAMES pwsh)
        if(discovered_powershell)
            set(${out_variable} "${discovered_powershell}" CACHE FILEPATH
                "PowerShell executable used by native CTest children")
        else()
            unset(${out_variable} CACHE)
        endif()
    endif()

    set(${out_variable} "${${out_variable}}" PARENT_SCOPE)
endfunction()
