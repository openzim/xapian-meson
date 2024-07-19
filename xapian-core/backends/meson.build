sources += files(
    'alltermslist.cc',
    'dbcheck.cc',
    'database.cc',
    'databasehelpers.cc',
    'databasereplicator.cc',
    'dbfactory.cc',
    'slowvaluelist.cc',
    'uuids.cc',
    'valuelist.cc'
)

if get_option('enable-backend-remote')
	sources += files('dbfactory_remote.cc')
endif

if get_option('enable-backend-chert') or get_option('enable-backend-glass')
    sources += files(
        'contiguousalldocspostlist.cc',
        'flint_lock.cc'
	)
endif

# # To add a new database backend:
# #
# # 1) Add lines to configure.ac to define the automake conditional
# #    "BUILD_BACKEND_NEWONE" and add NEWONE to the "for backend in" loop.
# # 2) Update include/xapian/version_h.cc to handle XAPIAN_HAS_NEWONE_BACKEND.
# # 3) Add "include backends/newone/Makefile.mk" to the list below.
# # 4) Write backends/newone/Makefile.mk - it should add files to noinst_HEADERS
# #    and lib_src conditional on BUILD_BACKEND_NEWONE.
# # 5) Update backends/dbfactory.cc.
# # 6) If it needs to support replication, update backends/databasereplicator.cc
# # 7) Write the backend code!

subdir('chert')
subdir('glass')
subdir('inmemory')
subdir('multi')
subdir('remote')
