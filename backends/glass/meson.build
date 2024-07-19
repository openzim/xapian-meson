if get_option('enable-backend-glass')
    sources += files(
        'glass_alldocspostlist.cc',
        'glass_alltermslist.cc',
        'glass_changes.cc',
        'glass_check.cc',
        'glass_compact.cc',
        'glass_cursor.cc',
        'glass_database.cc',
        'glass_dbcheck.cc',
        'glass_document.cc',
        'glass_freelist.cc',
        'glass_inverter.cc',
        'glass_metadata.cc',
        'glass_positionlist.cc',
        'glass_postlist.cc',
        'glass_spelling.cc',
        'glass_spellingwordslist.cc',
        'glass_synonym.cc',
        'glass_table.cc',
        'glass_termlist.cc',
        'glass_termlisttable.cc',
        'glass_valuelist.cc',
        'glass_values.cc',
        'glass_version.cc'
    )

    if get_option('enable-backend-remote')
        source += files('glass_databasereplicator.cc')
    endif
endif
