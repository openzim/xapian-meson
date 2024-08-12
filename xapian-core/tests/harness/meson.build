
harness_include_dirs = [include_directories('.')]

harness_srcs = [
  '../common/str.cc',
  '../common/errno_to_string.cc',
  'harness/backendmanager.cc',
  'harness/backendmanager_multi.cc',
  'harness/cputimer.cc',
  'harness/fdtracker.cc',
  'harness/index_utils.cc',
  'harness/scalability.cc',
  'harness/testrunner.cc',
  'harness/testsuite.cc',
  'harness/testutils.cc',
  'harness/unixcmds.cc'
]

uharness_srcs = [
  'harness/fdtracker.cc',
  'harness/utestsuite.cc'
]

if get_option('enable-backend-chert')
   harness_srcs += ['harness/backendmanager_chert.cc']
endif


if get_option('enable-backend-glass')
  harness_srcs += [
      'harness/backendmanager_glass.cc',
      'harness/backendmanager_singlefile.cc'
  ]
endif


if get_option('enable-backend-inmemory')
  harness_srcs += ['harness/backendmanager_inmemory.cc']
endif

if get_option('enable-backend-remote')
  harness_srcs += [
      'harness/backendmanager_remote.cc',
      'harness/backendmanager_remoteprog.cc',
      'harness/backendmanager_remotetcp.cc'
  ]
endif
