
subdir('harness')

collated_api_test_src = [
    'api_anydb.cc',
    'api_backend.cc',
    'api_closedb.cc',
    'api_collapse.cc',
    'api_compact.cc',
    'api_compactold.cc',
    'api_db.cc',
    'api_generated.cc',
    'api_geospatial.cc',
    'api_matchspy.cc',
    'api_metadata.cc',
    'api_nodb.cc',
    'api_none.cc',
    'api_opsynonym.cc',
    'api_opvalue.cc',
    'api_percentages.cc',
    'api_posdb.cc',
    'api_postingsource.cc',
    'api_qpbackend.cc',
    'api_query.cc',
    'api_queryparser.cc',
    'api_replacedoc.cc',
    'api_replicate.cc',
    'api_scalability.cc',
    'api_serialise.cc',
    'api_snippets.cc',
    'api_sorting.cc',
    'api_spelling.cc',
    'api_stem.cc',
    'api_termgen.cc',
    'api_transdb.cc',
    'api_unicode.cc',
    'api_valuestats.cc',
    'api_valuestream.cc',
    'api_weight.cc',
    'api_wrdb.cc'
]

# -------------------------
# API testing
test_api_exe = executable(
    'test_api',
    ['apitest.cc', 'dbcheck.cc'] + collated_api_test_src + harness_srcs,
    include_directories: include_dirs + harness_include_dirs,
    link_with: [libxapian, libgetopt] 
)

test_variants = [
    'inmemory',
    'multi',
    'singlefile',
]

if get_option('enable-backend-chert')
    test_variants += [
        'multi_chert',
        'chert'
    ]
endif

if get_option('enable-backend-glass')
    test_variants += [
        'multi_glass',
        'glass',
        'singlefile_glass'
    ]
endif

#foreach variant : test_variants
#    test(
#        'test_api_' + variant,
#        test_api_exe,
#        is_parallel: false,
#        args: ['-b', variant],
#        timeout: 240,
#        env : {
#            'srcdir' : meson.current_source_dir()
#        }
#    )
#endforeach

test(
    'test_api',
    test_api_exe,
    is_parallel: false,
    timeout: 480,
    env : {
        'srcdir' : meson.current_source_dir()
    }
)

# -------------------------
# Stem testing
test_stem_exe = executable(
    'test_stem',
    ['stemtest.cc'] + harness_srcs,
    include_directories: include_dirs + harness_include_dirs,
    link_with: [libxapian, libgetopt] 
)

test(
    'test_stem',
    test_stem_exe,
    env : {
        'srcdir' : meson.current_source_dir()
    }
)


# -------------------------
# Internal testing
test_internal_exe = executable(
    'test_internal',
    ['internaltest.cc'] + harness_srcs,
    include_directories: include_dirs + harness_include_dirs,
    link_with: [libxapian, libgetopt] 
)

test(
    'test_internal',
    test_internal_exe,
    env : {
        'srcdir' : meson.current_source_dir()
    }
)

# -------------------------
# Unit testing
test_unittest_exe = executable(
    'test_unittest',
    ['unittest.cc'] + uharness_srcs,
    include_directories: include_dirs + harness_include_dirs,
    link_with: [libxapian, libgetopt] 
)

test(
    'test_unittest',
    test_unittest_exe,
    env : {
        'srcdir' : meson.current_source_dir()
    }
)