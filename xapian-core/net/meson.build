sources += files(
    'length.cc',
    'serialise.cc'
)

if get_option('enable-backend-remote')
    sources += files(
        'progclient.cc',
        'remoteconnection.cc',
        'remoteserver.cc',
        'remotetcpclient.cc',
        'remotetcpserver.cc',
        'replicatetcpclient.cc',
        'replicatetcpserver.cc',
        'serialise-error.cc',
        'tcpclient.cc',
        'tcpserver.cc'
    )
endif
