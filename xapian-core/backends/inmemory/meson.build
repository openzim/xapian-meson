if get_option('enable-backend-inmemory')
    sources += files(
        'inmemory_alltermslist.cc',
        'inmemory_database.cc',
        'inmemory_document.cc',
        'inmemory_positionlist.cc'
	)
else
    sources += files('inmemory_positionlist.cc')
endif
