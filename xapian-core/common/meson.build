include_dirs += include_directories('.')

sources += files(
    'bitstream.cc',
    'closefrom.cc',
    'debuglog.cc',
    'errno_to_string.cc',
    'fileutils.cc',
    'io_utils.cc',
    'keyword.cc',
    'msvc_dirent.cc',
    'omassert.cc',
    'posixy_wrapper.cc',
    'replicate_utils.cc',
    'safe.cc',
    'serialise-double.cc',
    'str.cc'
)


if get_option('enable-backend-chert') or get_option('enable-backend-glass')
    sources += files('compression_stream.cc')
endif

if get_option('enable-backend-remote')
    sources += files('socket_utils.cc')
endif


libgetopt = static_library(
   'getopt',
   'getopt.cc',
    include_directories: include_dirs
)