project('xapian-core', 'cpp', version:'1.4.23',
default_options : ['cpp_std=c++11'])


add_project_arguments('-O', language: 'cpp')
add_project_arguments('-DHAVE_CONFIG_H', language: 'cpp')

pkg = import('pkgconfig')


cc = meson.get_compiler('cpp')
cdata = configuration_data()

windows_build = build_machine.system() == 'windows'
stl_debug = (windows_build and get_option('buildtype') == 'debug') or (not windows_build and get_option('cpp_debugstl')) 
cdata.set10('DEBUG_MODE', stl_debug)
if stl_debug
  add_project_arguments('-D_GLIBCXX_DEBUG', language: 'cpp')
endif
cdata.set10('HAVE_CXX11', true)

cdata.set_quoted('VERSION', meson.project_version())
cdata.set_quoted('PACKAGE', 'xapian-core')
cdata.set_quoted('PACKAGE_BUGREPORT', 'https://xapian.org/bugs')
cdata.set_quoted('PACKAGE_NAME', 'xapian-core')
cdata.set_quoted('PACKAGE_STRING', 'xapian-core 1.4.23')
cdata.set_quoted('PACKAGE_TARNAME', 'xapian-core')
cdata.set_quoted('PACKAGE_URL', '')
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())

ver_arr = meson.project_version().split('.')
cdata.set_quoted('XAPIAN_VERSION', meson.project_version())
cdata.set('XAPIAN_MAJOR_VERSION', ver_arr[0])
cdata.set('XAPIAN_MINOR_VERSION', ver_arr[1])
cdata.set('XAPIAN_REVISION', ver_arr[2])

cdata.set('XAPIAN_HAS_CHERT_BACKEND', get_option('enable-backend-chert'))
cdata.set('XAPIAN_HAS_GLASS_BACKEND', get_option('enable-backend-glass'))
cdata.set('XAPIAN_HAS_INMEMORY_BACKEND', get_option('enable-backend-inmemory'))
cdata.set('XAPIAN_HAS_REMOTE_BACKEND', get_option('enable-backend-remote'))

cdata.set('WORDS_BIGENDIAN', host_machine.endian() == 'big')


check_functions = [
  ['HAVE_CLOCK_GETTIME', 'clock_gettime', '#include<time.h>'],
  ['HAVE_CLOSEFROM', 'closefrom', '#include <unistd.h>'],
  ['HAVE_FDATASYNC', 'fdatasync', '#include <unistd.h>'],
  ['HAVE_FSYNC', 'fsync', '#include <unistd.h>'],
  ['HAVE_FTIME', 'ftime', '#include <sys/timeb.h>'],
  ['HAVE_FTRUNCATE', 'ftruncate', '#include <unistd.h>'],
  ['HAVE_GETDIRENTRIES', 'getdirentries', '#include <dirent.h>'],
  ['HAVE_GETHOSTNAME', 'gethostname', '#include <unistd.h>'],
  ['HAVE_GETRLIMIT', 'getrlimit', '#include <sys/resource.h>'],
  ['HAVE_GETRUSAGE', 'getrusage', '#include <sys/time.h>\n#include <sys/resource.h>'],
  ['HAVE_GETTIMEOFDAY', 'gettimeofday', '#include<sys/time.h>'],
  ['HAVE_LINK', 'link', '#include<unistd.h>'],
  ['HAVE_NANOSLEEP', 'nanosleep', '#include<time.h>'],
  ['HAVE_NFTW', 'nftw', '#include <ftw.h>'],
  ['HAVE_POLL', 'poll', '#include<poll.h>'],
  ['HAVE_POSIX_FADVISE', 'posix_fadvise', '#include <fcntl.h>'],
  ['HAVE_PREAD', 'pread', '#include<unistd.h>'],
  ['HAVE_PWRITE', 'pwrite', '#include<unistd.h>'],
  ['HAVE_SETENV', 'setenv', '#include<stdlib.h>'],
  ['HAVE_SIGACTION', 'sigaction', '#include<signal.h>'],
  ['HAVE_SLEEP', 'sleep', '#include<unistd.h>'],
  ['HAVE_STRERRORDESC_NP', 'strerrordesc_np', '#include <string.h>'],
  ['HAVE_STRERROR_R', 'strerror_r', '#include<string.h>'],
  ['HAVE_SYSCONF', 'sysconf', '#include<unistd.h>'],
  ['HAVE_SYS_ERRLIST_AND_SYS_NERR', 'sys_errlist', '#include<cstdlib>'],
  ['HAVE_TIMER_CREATE', 'timer_create', '#include <time.h>'],
  ['HAVE_TIMES', 'times', '#include<sys/times.h>'],
# check token ['HAVE_VALGRIND']
  ['HAVE__PUTENV_S', '_putenv_s(const char*, const char*)', '#include <stdlib.h>'],
  ['HAVE__SYS_ERRLIST_AND__SYS_NERR', '_sys_errlist', '#include<cstdlib>'],
  ['HAVE_DECL___BUILTIN_EXPECT', '__builtin_expect', '']
]

win32_need_lws2_32 = false
if get_option('enable-backend-remote')
  if cc.get_id() == 'msvc'
    add_project_arguments(['-DWINVER=0x600', '-D_WIN32_WINNT=0x600'], lang='cpp')
    win32_need_lws2_32 = true
  else
    check_functions += [
      ['HAVE_FORK', 'fork', '#include <unistd.h>'],
      ['HAVE_SOCKETPAIR', 'socketpair', '#include<sys/socket.h>']
    ]
  endif
endif


if host_machine.system() == 'windows'
  cdata.set_quoted('DIR_SEPS', '/\\\\')
  cdata.set('DIR_SEPS_LIST', '{ \'/\', \'\\\\\'}')
else
  cdata.set('DIR_SEPS', '\'/\'')
  cdata.set('DIR_SEPS_LIST', '{\'/\'}')
endif

# Check if ftime returns void (as is does on mingw)
cdata.set(
  'FTIME_RETURNS_VOID',
  not cc.compiles(
    '#include <sys/timeb.h>\nstruct timeb tp; int i = ftime(&tp);',
    name: 'Return type of ftime'
  )
)

if cc.has_function_attribute('visibility:default') and cc.has_function_attribute('visibility:internal')
  cdata.set('XAPIAN_ENABLE_VISIBILITY', true)
  add_project_arguments(['-fvisibility=hidden', '-fvisibility-inlines-hidden'], language: 'cpp')
endif

check_headers = [
  ['HAVE_CXXABI_H', 'cxxabi.h'],
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_FCNTL_H', 'fcntl.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_LIMITS_H', 'limits.h'],
  ['HAVE_POLL_H', 'poll.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDIO_H', 'stdio.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_RESOURCE_H', 'sys/resource.h'],
  ['HAVE_SYS_SELECT_H', 'sys/select.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_SYS_UTSNAME_H', 'sys/utsname.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
  ['HAVE_UUID_H', 'uuid.h'],
  ['HAVE_UUID_UUID_H', 'uuid/uuid.h'],
  ['HAVE_VALGRIND_MEMCHECK_H', 'valgrind/memcheck.h'],
  ['HAVE_ZLIB_H', 'zlib.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach


foreach f : check_functions
  if cc.has_function(f.get(1), prefix : f.get(2))
    cdata.set10(f.get(0), true)
  endif
endforeach

check_decls = [
  ['HAVE_DECL_LOG2',                   'log2',                   '((double)0)', '#include <cmath>\nusing namespace std;'],
  ['HAVE_DECL_EXP10',                  'exp10',                  '((double)0)', '#include <cmath>\nusing namespace std;'],
  ['HAVE_DECL___EXP10',                '__exp10',                '((double)0)', '#include <cmath>\nusing namespace std;'],
  ['HAVE_DECL_SIGLONGJMP',             'siglongjmp',             '', '#include <setjmp.h>'],
  ['HAVE_DECL_SIGSETJMP',              'sigsetjmp',              '', '#include <setjmp.h>'],
  ['HAVE_DECL_STRERROR_R',             'strerror_r',             '', '#include <string.h>'],
  ['HAVE_DECL__ADDCARRY_U32',          '_addcarry_u32',          '((unsigned char)0, (unsigned)0, (unsigned)0, (unsigned*)0)', '#include <intrin.h>'],
  ['HAVE_DECL__ADDCARRY_U64',          '_addcarry_u64',          '((unsigned char)0, (unsigned __int64)0, (unsigned __int64)0, (unsigned __int64*)0)', '#include <intrin.h>'],
  ['HAVE_DECL__SUBBORROW_U32',         '_subborrow_u32',         '((unsigned char)0, (unsigned)0, (unsigned)0, (unsigned*)0)', '#include <intrin.h>'],
  ['HAVE_DECL__SUBBORROW_U64',         '_subborrow_u64',         '((unsigned char)0, (unsigned __int64)0, (unsigned __int64)0, (unsigned __int64*)0)', '#include <intrin.h>'],
  ['HAVE_DECL__BYTESWAP_USHORT',       '_byteswap_ushort',       '', '#include <stdlib.h>'],
  ['HAVE_DECL__BYTESWAP_ULONG',        '_byteswap_ulong',        '', '#include <stdlib.h>'],
  ['HAVE_DECL__BYTESWAP_UINT64',       '_byteswap_uint64',       '', '#include <stdlib.h>'],
  ['HAVE_DECL__PUTENV_S',              '_putenv_s',              '', '#include <stdlib.h>'],
  ['HAVE_DECL___BUILTIN_ADD_OVERFLOW', '__builtin_add_overflow', '((int)0, (int)0, (int*)0)', ''],
  ['HAVE_DECL___BUILTIN_SUB_OVERFLOW', '__builtin_sub_overflow', '((int)0, (int)0, (int*)0)', ''],
  ['HAVE_DECL___BUILTIN_MUL_OVERFLOW', '__builtin_mul_overflow', '((int)0, (int)0, (int*)0)', ''],
  ['HAVE_DECL___BUILTIN_BSWAP16',      '__builtin_bswap16',      '((uint16_t)0)', '#include <stdint.h>'],
  ['HAVE_DECL___BUILTIN_BSWAP32',      '__builtin_bswap32',      '((uint32_t)0)', '#include <stdint.h>'],
  ['HAVE_DECL___BUILTIN_BSWAP64',      '__builtin_bswap64',      '((uint64_t)0)', '#include <stdint.h>'],
  ['HAVE_DECL___BUILTIN_CLZ',          '__builtin_clz',          '', ''],
  ['HAVE_DECL___BUILTIN_CLZL',         '__builtin_clzl',         '', ''],
  ['HAVE_DECL___BUILTIN_CLZLL',        '__builtin_clzll',        '', ''],
  ['HAVE_DECL___BUILTIN_CTZ',          '__builtin_ctz',          '', ''],
  ['HAVE_DECL___BUILTIN_CTZL',         '__builtin_ctzl',         '', ''],
  ['HAVE_DECL___BUILTIN_CTZLL',        '__builtin_ctzll',        '', ''],
  ['HAVE_DECL___BUILTIN_EXPECT',       '__builtin_expect',       '((long)0, (long)0)', ''],
  ['HAVE_DECL___BUILTIN_POPCOUNT',     '__builtin_popcount',     '((unsigned)0)', ''],
  ['HAVE_DECL___BUILTIN_POPCOUNTL',    '__builtin_popcountl',    '((unsigned long)0)', ''],
  ['HAVE_DECL___BUILTIN_POPCOUNTLL',   '__builtin_popcountll',   '((unsigned long long)0)', ''],
  ['HAVE_DECL___POPCNT',               '__popcnt',               '', '#include <intrin.h>'],
  ['HAVE_DECL___POPCNT64',             '__popcnt64',             '', '#include <intrin.h>'],
]

decl_template = '''@2@
int main (void) {
  #ifndef @0@
  (void) @0@@1@;
  #endif

  return 0;
}
'''

foreach f: check_decls
  has_decl = cc.compiles(decl_template.format(f.get(1), f.get(2), f.get(3)))
  cdata.set10(f.get(0), has_decl)
endforeach

has_builtin_exp10 = cc.links(
  '''#include <cstdio>
#include <ctime>
using namespace std;
int main(void) {
  printf("%f", __builtin_exp10(double(time(NULL)*1e-8)));
}
  '''
)
cdata.set('HAVE___BUILTIN_EXP10', has_builtin_exp10)

cdata.set('SIZEOF_INT', cc.sizeof('int'))
cdata.set('SIZEOF_LONG', cc.sizeof('long'))
cdata.set('SIZEOF_LONG_LONG', cc.sizeof('long long'))
cdata.set('SIZEOF_SHORT', cc.sizeof('short'))



if get_option('enable-backend-chert') or get_option('enable-backend-glass')
  zlib_dep = dependency('zlib')
  if host_machine.system() == 'windows'
    cdata.set('USE_WIN32_UUID_API', 1)
    win32_need_lws2_32 = true
    add_project_link_arguments('-lrpcrt4', language: 'cpp')
    uuid_dep = dependency('', required: false)
    if cc.get_id() != 'cygwin'
      win32_need_lws2_32 = true
    endif
  else
    uuid_dep = dependency('uuid')
  endif
else
  zlib_dep = dependency('', required: false)
endif

if win32_need_lws2_32
  add_project_link_arguments('-lws2_32', language: 'cpp')
endif

if host_machine.cpu_family() == 'x86'
  if get_option('enable-sse')
    if gcc
      add_project_arguments('-mfpmath=sse -m{} -mtune=generic', language: 'ccp')
    elif msvc
      add_project_arguments('-arch:SSE', language: 'cpp')
    else
      # [TODO] Test for sun
    endif
  else
    if msvc
      add_project_arguments('-arch:IA32', language: 'cpp')
    endif
  endif
endif

size_short = cc.sizeof('short')
size_int = cc.sizeof('int')
size_long = cc.sizeof('long')
size_long_long = cc.sizeof('long long')

if size_short == 4
  INT32_T = 'short'
elif size_int == 4
  INT32_T = 'int'
elif size_long == 4
  INT32_T = 'long'
else
  error('No 32 bit integer type found')
endif

if size_int == 8
  INT64_T = 'int'
elif size_long == 8
  INT64_T = 'long'
elif size_long_long == 8
  INT64_T = 'long long'
else
  error('No 64 bit integer type found')
endif

if get_option('enable-64bit-docid')
  cdata.set('XAPIAN_DOCID_BASE_TYPE', INT64_T)
else
  cdata.set('XAPIAN_DOCID_BASE_TYPE', INT32_T)
endif

if get_option('enable-64bit-termcount')
  cdata.set('XAPIAN_TERMCOUNT_BASE_TYPE', INT64_T)
else
  cdata.set('XAPIAN_TERMCOUNT_BASE_TYPE', INT32_T)
endif

if get_option('enable-64bit-termpos')
  cdata.set('XAPIAN_TERMPOS_BASE_TYPE', INT64_T)
else
  cdata.set('XAPIAN_TERMPOS_BASE_TYPE', INT32_T)
endif

cdata.set('XAPIAN_TOTALLENGTH_TYPE', 'unsigned ' + INT64_T)
cdata.set('XAPIAN_REVISION_TYPE', 'unsigned ' + INT64_T)



# TODO handle backend-remote

# TODO handle enable-assertion
# TODO handle enable-log


configure_file(input : 'config.h.meson',
  output : 'config.h',
  configuration : cdata)

sources = []
include_dirs = [include_directories('.')]

subdir('api')
#subdir('bin')
subdir('backends')
subdir('common')
#subdir('examples')
subdir('expand')
subdir('geospatial')
subdir('include')
subdir('languages')
subdir('matcher')
subdir('net')
subdir('queryparser')
subdir('unicode')
subdir('weight')


deps = [zlib_dep, uuid_dep]

libxapian = library(
    'xapian',
    sources,
    version: '30.12.4',
    soversion: 30,
    dependencies: deps,
    include_directories: include_dirs,
    link_args : ['-Wl,--no-undefined'],
    install: true
)


pkg.generate(
    libxapian,
    name: 'Xapian',
    description: 'Search Engine Library',
    url: 'https://xapian.org/',
    filebase: 'xapian-core',
)

subdir('tests')