# map cmake options to compiler defines and do miscellaneous further configuration work
# cmake options are defined in cmake/create_options.cmake

# features

set ( SHELL ${ENABLE_SHELL} )
set ( UNIT_TEST ${ENABLE_UNIT_TESTS} )
set ( PIGLET ${ENABLE_PIGLET} )

if ( NOT ENABLE_COREFILES )
    set ( NOCOREFILE ON )
endif ( NOT ENABLE_COREFILES )

set ( _LARGEFILE_SOURCE ${ENABLE_LARGE_PCAP} )
set ( USE_STDLOG ${ENABLE_STDLOG} )
set ( USE_TSC_CLOCK ${ENABLE_TSC_CLOCK} )
set ( NO_PROFILER ${DISABLE_SNORT_PROFILER} )
set ( DEEP_PROFILING ${ENABLE_DEEP_PROFILING} )
set ( NO_MEM_MGR ${DISABLE_MEMORY_MANAGER} )

if ( ENABLE_LARGE_PCAP )
    set ( _FILE_OFFSET_BITS 64 )
endif ( ENABLE_LARGE_PCAP )

# documentation

if ( NOT ASCIIDOC_FOUND )
    get_property ( MAKE_HTML_DOC_HELP_STRING CACHE "MAKE_HTML_DOC" PROPERTY HELPSTRING )
    set ( MAKE_HTML_DOC OFF CACHE BOOL ${MAKE_HTML_DOC_HELP_STRING} FORCE )
endif()

if ( NOT (DBLATEX_FOUND AND ASCIIDOC_FOUND) )
    get_property ( MAKE_PDF_DOC_HELP_STRING CACHE "MAKE_PDF_DOC" PROPERTY HELPSTRING )
    set ( MAKE_PDF_DOC OFF CACHE BOOL ${MAKE_PDF_DOC_HELP_STRING} FORCE )
endif()

if ( NOT (W3M_FOUND AND ASCIIDOC_FOUND) )
    get_property ( MAKE_TEXT_DOC_HELP_STRING CACHE "MAKE_TEXT_DOC" PROPERTY HELPSTRING )
    set ( MAKE_TEXT_DOC OFF CACHE BOOL ${MAKE_TEXT_DOC_HELP_STRING} FORCE )
endif()

# security

if ( ENABLE_HARDENED_BUILD )

    check_cxx_compiler_flag ( "-Wdate-time" HAS_WDATE_TIME_CPPFLAG )
    if ( HAS_WDATE_TIME_CPPFLAG )
        string ( APPEND HARDENED_CXX_FLAGS " -Wdate-time" )
    endif ()

    check_cxx_compiler_flag ( "-D_FORTIFY_SOURCE=2" HAS_FORTIFY_SOURCE_2_CPPFLAG )
    if ( HAS_FORTIFY_SOURCE_2_CPPFLAG )
        string ( APPEND HARDENED_CXX_FLAGS " -D_FORTIFY_SOURCE=2" )
    endif ()

    check_cxx_compiler_flag ( "-fstack-protector-strong" HAS_FSTACK_PROTECTOR_STRONG_CXXFLAG )
    if ( HAS_FSTACK_PROTECTOR_STRONG_CXXFLAG )
        string ( APPEND HARDENED_CXX_FLAGS " -fstack-protector-strong" )
    endif ()

    check_cxx_compiler_flag ( "-Wformat" HAS_WFORMAT_CXXFLAG )
    if ( HAS_WFORMAT_CXXFLAG )
        string ( APPEND HARDENED_CXX_FLAGS " -Wformat" )
    endif ()

    check_cxx_compiler_flag ( "-Werror=format-security" HAS_WERROR_FORMAT_SECURITY_CXXFLAG )
    if ( HAS_WERROR_FORMAT_SECURITY_CXXFLAG )
        string ( APPEND HARDENED_CXX_FLAGS " -Werror=format-security" )
    endif ()

    set ( CMAKE_REQUIRED_FLAGS "-Wl,-z,relro" )
    check_cxx_compiler_flag ( "" HAS_ZRELRO_LDFLAG )
    unset ( CMAKE_REQUIRED_FLAGS )
    if ( HAS_ZRELRO_LDFLAG )
        string ( APPEND HARDENED_LINKER_FLAGS " -Wl,-z,relro" )
    endif ()

    set ( CMAKE_REQUIRED_FLAGS "-Wl,-z,now" )
    check_cxx_compiler_flag ( "-Wl,-z,now" HAS_ZNOW_LDFLAG )
    unset ( CMAKE_REQUIRED_FLAGS )
    if ( HAS_ZNOW_LDFLAG )
        string ( APPEND HARDENED_LINKER_FLAGS " -Wl,-z,now" )
    endif ()

endif ( ENABLE_HARDENED_BUILD )

if ( ENABLE_PIE )
    set ( CMAKE_REQUIRED_FLAGS "-fPIE -pie" )
    check_cxx_compiler_flag ( "-fPIE" HAS_PIE_SUPPORT )
    unset ( CMAKE_REQUIRED_FLAGS )
    if ( HAS_PIE_SUPPORT )
        string ( APPEND HARDENED_CXX_FLAGS " -fPIE" )
        string ( APPEND HARDENED_LINKER_FLAGS " -fPIE -pie" )
    endif ()
endif ( ENABLE_PIE )

# debugging

set ( DEBUG_MSGS ${ENABLE_DEBUG_MSGS} )

# FIXIT-L Properly handle NDEBUG through CMAKE_BUILD_TYPE
if ( ENABLE_DEBUG )
    string ( APPEND DEBUGGING_C_FLAGS " -g -DDEBUG" )
else ()
    string ( APPEND DEBUGGING_C_FLAGS " -DNDEBUG" )
endif ( ENABLE_DEBUG )

if ( ENABLE_GDB )
    string ( APPEND DEBUGGING_C_FLAGS " -g -ggdb" )
endif ( ENABLE_GDB )

if ( ENABLE_PROFILE AND CMAKE_COMPILER_IS_GNUCXX )
    string ( APPEND DEBUGGING_C_FLAGS " -pg" )
endif ( ENABLE_PROFILE AND CMAKE_COMPILER_IS_GNUCXX )

# ASAN and TSAN are mutually exclusive, so have them absolutely set SANITIZER_*_FLAGS first.
if ( ENABLE_ADDRESS_SANITIZER )
    set ( ASAN_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer" )
    set ( ASAN_LINKER_FLAGS "-fsanitize=address" )
    set ( CMAKE_REQUIRED_FLAGS "${ASAN_LINKER_FLAGS}" )
    check_cxx_compiler_flag ( "${ASAN_CXX_FLAGS}" HAVE_ADDRESS_SANITIZER )
    unset ( CMAKE_REQUIRED_FLAGS )
    if ( HAVE_ADDRESS_SANITIZER )
        set ( SANITIZER_CXX_FLAGS "${ASAN_CXX_FLAGS}" )
        set ( SANITIZER_LINKER_FLAGS "${ASAN_LINKER_FLAGS}" )
    else ()
        message ( SEND_ERROR "Could not enable the address sanitizer!" )
    endif ()
endif ( ENABLE_ADDRESS_SANITIZER )

if ( ENABLE_THREAD_SANITIZER )
    set ( TSAN_CXX_FLAGS "-fsanitize=thread -fno-omit-frame-pointer" )
    set ( TSAN_LINKER_FLAGS "-fsanitize=thread" )
    set ( CMAKE_REQUIRED_FLAGS "${TSAN_LINKER_FLAGS}" )
    check_cxx_compiler_flag ( "${TSAN_CXX_FLAGS}" HAVE_THREAD_SANITIZER )
    unset ( CMAKE_REQUIRED_FLAGS )
    if ( HAVE_THREAD_SANITIZER )
        set ( SANITIZER_CXX_FLAGS "${TSAN_CXX_FLAGS}" )
        set ( SANITIZER_LINKER_FLAGS "${TSAN_LINKER_FLAGS}" )
    else ()
        message ( SEND_ERROR "Could not enable the thread sanitizer!" )
    endif ()
endif ( ENABLE_THREAD_SANITIZER )

if ( ENABLE_UB_SANITIZER )
    set ( UBSAN_CXX_FLAGS "-fsanitize=undefined -fno-sanitize=alignment -fno-omit-frame-pointer" )
    set ( UBSAN_LINKER_FLAGS "-fsanitize=undefined -fno-sanitize=alignment" )
    set ( CMAKE_REQUIRED_FLAGS "${UBSAN_LINKER_FLAGS}" )
    check_cxx_compiler_flag ( "${UBSAN_CXX_FLAGS}" HAVE_UB_SANITIZER )
    unset ( CMAKE_REQUIRED_FLAGS )
    if ( HAVE_UB_SANITIZER )
        string ( APPEND SANITIZER_CXX_FLAGS " ${UBSAN_CXX_FLAGS}" )
        string ( APPEND SANITIZER_LINKER_FLAGS " ${UBSAN_LINKER_FLAGS}" )
    else ()
        message ( SEND_ERROR "Could not enable the undefined behavior sanitizer!" )
    endif ()
endif ( ENABLE_UB_SANITIZER )

if ( ENABLE_TCMALLOC )
    if ( ENABLE_ADDRESS_SANITIZER )
        message ( SEND_ERROR "TCMalloc cannot be used at the same time as address sanitizer!" )
    endif ()
    find_package ( TCMalloc REQUIRED )
    set ( TCMALLOC_C_FLAGS "-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free" )
    set ( HAVE_TCMALLOC "1" )
endif ( ENABLE_TCMALLOC )

if ( ENABLE_CODE_COVERAGE )
    include(${CMAKE_MODULE_PATH}/CodeCoverage.cmake)
endif ( ENABLE_CODE_COVERAGE )


# Accumulate extra flags and libraries
#[[
message("
    HARDENED_CXX_FLAGS = ${HARDENED_CXX_FLAGS}
    HARDENED_LINKER_FLAGS = ${HARDENED_LINKER_FLAGS}
    DEBUGGING_C_FLAGS = ${DEBUGGING_C_FLAGS}
    SANITIZER_CXX_FLAGS = ${SANITIZER_CXX_FLAGS}
    SANITIZER_LINKER_FLAGS = ${SANITIZER_LINKER_FLAGS}
    COVERAGE_COMPILER_FLAGS = ${COVERAGE_COMPILER_FLAGS}
    COVERAGE_LINKER_FLAGS = ${COVERAGE_LINKER_FLAGS}
    COVERAGE_LIBRARIES = ${COVERAGE_LIBRARIES}
    TCMALLOC_C_FLAGS = ${TCMALLOC_C_FLAGS}
    TCMALLOC_LIBRARIES = ${TCMALLOC_LIBRARIES}
")
]]

#dpdk link flags
string ( APPEND DPDK_LINKER_FLAGS "-lpthread -Wl,--no-as-needed -Wl,-export-dynamic -Wl,-lrte_flow_classify -Wl,-lrte_pipeline -Wl,-lrte_table -Wl,-lrte_port -Wl,-lrte_pdump -Wl,-lrte_distributor -Wl,-lrte_ip_frag  -Wl,-lrte_gro -Wl,-lrte_gso -Wl,-lrte_meter -Wl,-lrte_lpm -Wl,--whole-archive -Wl,-lrte_acl -Wl,--no-whole-archive -Wl,-lrte_jobstats -Wl,-lrte_metrics   -Wl,-lrte_bitratestats -Wl,-lrte_latencystats -Wl,-lrte_power -Wl,-lrte_timer -Wl,-lrte_efd -Wl,--whole-archive -Wl,-lrte_cfgfile -Wl,-lrte_hash  -Wl,-lrte_member -Wl,-lrte_vhost -Wl,-lrte_kvargs -Wl,-lrte_mbuf -Wl,-lrte_net -Wl,-lrte_ethdev -Wl,-lrte_cryptodev -Wl,-lrte_security  -Wl,-lrte_eventdev -Wl,-lrte_mempool -Wl,-lrte_mempool_ring -Wl,-lrte_ring -Wl,-lrte_pci -Wl,-lrte_eal -Wl,-lrte_cmdline -Wl,-lrte_reorder  -Wl,-lrte_sched -Wl,-lrte_kni -Wl,-lrte_bus_pci -Wl,-lrte_bus_vdev -Wl,-lrte_mempool_stack -Wl,-lrte_pmd_af_packet -Wl,-lrte_pmd_bond  -Wl,-lrte_pmd_e1000 -Wl,-lrte_pmd_failsafe -Wl,-lrte_pmd_i40e -Wl,-lrte_pmd_ixgbe -Wl,-lrte_pmd_kni -Wl,-lrte_pmd_null -Wl,-lrte_pmd_ring  -Wl,-lrte_pmd_softnic -Wl,-lrte_pmd_tap -Wl,-lrte_pmd_virtio -Wl,-lrte_pmd_vhost -Wl,-lrte_pmd_null_crypto -Wl,-lrte_pmd_crypto_scheduler   -Wl,-lrte_pmd_skeleton_event -Wl,-lrte_pmd_sw_event -Wl,--no-whole-archive -Wl,-lrt -Wl,-lm -Wl,-lnuma -Wl,-ldl -Wl,-export-dynamic  -Wl,-L/home/tools/dpdk-stable-19.11.5/arm64-armv8a-linuxapp-gcc/lib/ /home/tools/dpdk-stable-19.11.5/arm64-armv8a-linuxapp-gcc/lib/libdpdk.a " )

set ( EXTRA_C_FLAGS "${EXTRA_C_FLAGS}  ${HARDENED_CXX_FLAGS} ${DEBUGGING_C_FLAGS} ${SANITIZER_CXX_FLAGS} ${TCMALLOC_C_FLAGS} ${COVERAGE_COMPILER_FLAGS}" )
set ( EXTRA_CXX_FLAGS "${EXTRA_CXX_FLAGS} ${HARDENED_CXX_FLAGS} ${DEBUGGING_C_FLAGS} ${SANITIZER_CXX_FLAGS} ${TCMALLOC_C_FLAGS} ${COVERAGE_COMPILER_FLAGS}" )
set ( EXTRA_LINKER_FLAGS "${EXTRA_LINKER_FLAGS}  ${DPDK_LINKER_FLAGS} ${HARDENED_LINKER_FLAGS} ${SANITIZER_LINKER_FLAGS} ${COVERAGE_LINKER_FLAGS}" )
foreach (EXTRA_LIBRARY IN LISTS COVERAGE_LIBRARIES TCMALLOC_LIBRARIES )
    list ( APPEND EXTRA_LIBRARIES ${EXTRA_LIBRARY} )
endforeach ()
