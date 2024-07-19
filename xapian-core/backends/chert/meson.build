if get_option('enable-backend-chert')
    sources += files(
        'chert_alldocsmodifiedpostlist.cc',
        'chert_alldocspostlist.cc',
        'chert_alltermslist.cc',
        'chert_btreebase.cc',
        'chert_check.cc',
        'chert_compact.cc',
        'chert_cursor.cc',
        'chert_database.cc',
        'chert_dbcheck.cc',
        'chert_dbstats.cc',
        'chert_document.cc',
        'chert_metadata.cc',
        'chert_modifiedpostlist.cc',
        'chert_positionlist.cc',
        'chert_postlist.cc',
        'chert_record.cc',
        'chert_spelling.cc',
        'chert_spellingwordslist.cc',
        'chert_synonym.cc',
        'chert_table.cc',
        'chert_termlist.cc',
        'chert_termlisttable.cc',
        'chert_valuelist.cc',
        'chert_values.cc',
        'chert_version.cc'
    )


    if get_option('enable-backend-remote')
        sources += files('chert_databasereplicator.cc')
    endif

endif
