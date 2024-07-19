include_dirs += [include_directories('.')]

snowball_sources = files(
    'arabic.cc',
    'armenian.cc',
    'basque.cc',
    'catalan.cc',
    'danish.cc',
    'dutch.cc',
    'english.cc',
    'earlyenglish.cc',
    'finnish.cc',
    'french.cc',
    'german2.cc',
    'german.cc',
    'hungarian.cc',
    'indonesian.cc',
    'irish.cc',
    'italian.cc',
    'kraaij_pohlmann.cc',
    'lithuanian.cc',
    'lovins.cc',
    'nepali.cc',
    'norwegian.cc',
    'porter.cc',
    'portuguese.cc',
    'romanian.cc',
    'russian.cc',
    'spanish.cc',
    'swedish.cc',
    'tamil.cc',
    'turkish.cc'
)

install_subdir('stopwords', install_dir: get_option('datadir') / 'xapian-core')

sources +=  snowball_sources
sources += files(
    'stem.cc',
	'steminternal.cc'
)
